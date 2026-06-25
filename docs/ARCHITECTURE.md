# HdRestir — Architecture

Companion to [README.md](../README.md). Covers class structure, data flow, and the package dependency rules that govern how the codebase should be split into independent libraries.

---

## Hydra integration

HdRestir plugs into OpenUSD's Hydra rendering framework as a custom `HdRenderDelegate`. Hydra drives the rendering loop: it syncs scene prims into the delegate and calls `Execute()` on the render pass each frame. The delegate creates the render pass at startup and owns the `Renderer` and scene state.

```mermaid
graph LR
    usdview["usdview / HdEngine"]
    delegate["HdRestirRenderDelegate"]
    pass["HdRestirRenderPass"]
    renderer["Renderer"]
    pipeline["RenderPipeline"]

    usdview -->|"① Sync(prims)"| delegate
    usdview -->|"② Execute() — each frame"| pass
    delegate -.->|"creates"| pass
    pass -->|"Render(job, scene)"| renderer
    renderer -->|"Execute(ctx)"| pipeline
```

---

## Pipeline structure

Both pipelines are compiled at runtime from a declared list of passes. The compiler walks the pass list in reverse, keeping only passes whose outputs are reachable from the requested AOVs — so requesting only `color` skips the depth pass automatically.

```mermaid
graph LR
    RC[RaycastPass]
    PT[PathTracePass]
    RPT[RISPathTracePass]
    AC[AccumulationPass]
    UP["UpscalePass †"]

    RC -->|PathTracer| PT
    RC -->|RIS| RPT
    PT --> AC
    RPT --> AC
    AC --> UP
```

† included only when `resolutionLevel > 0`.

A `PathTracerPostProcess` variant (with `DenoiserPass` and `PostProcessPass`) is fully implemented but not registered as a selectable pipeline yet.

---

## Pass and integrator class hierarchy

The key design point: swapping the lighting algorithm requires only changing the `IDirectLightIntegrator` inside `PathIntegrator`. The pipeline, accumulation, and all other passes are identical between PathTracer and RIS.

```mermaid
classDiagram
    class RenderPass {
        <<abstract>>
        +Inputs() vector~string~
        +Outputs() vector~string~
        #_execute(ctx)*
    }

    class IntegrationPass {
        #_integrator : IIntegrator
        #_execute(ctx)
    }

    class IIntegrator {
        <<interface>>
        +Li(isect, scene, rng, lambda, provider, callId) SampledSpectrum
        +GetBufferStager() IBufferStager*
    }

    class IDirectLightIntegrator {
        <<interface>>
        +Li(…, bsdfConnection, callId) SampledSpectrum
    }

    class PathIntegrator {
        -_factory : IDirectLightIntegratorFactory
        -_maxDepth : int
        +Li(…)
        +GetBufferStager()
    }

    class MisDirectLightIntegrator {
        -_sampler : ILightSampler
        +Li(…)
    }

    class RisDirectLightIntegrator {
        -_candidateCount : int
        -_useReservoir : bool
        -_sampler : ILightSampler
        +Li(…)
        +GetBufferStager()
    }

    class ILightSampler {
        <<interface>>
        +Sample(scene, rng) LightSample
    }

    class UniformLightSampler

    RenderPass      <|-- IntegrationPass
    RenderPass      <|-- RaycastPass
    RenderPass      <|-- AccumulationPass
    RenderPass      <|-- DenoiserPass
    RenderPass      <|-- PostProcessPass
    RenderPass      <|-- UpscalePass
    RenderPass      <|-- DebugOverlayPass
    IntegrationPass <|-- PathTracePass
    IntegrationPass <|-- RISPathTracePass

    IIntegrator             <|-- PathIntegrator
    IIntegrator             <|-- IDirectLightIntegrator
    IDirectLightIntegrator  <|-- MisDirectLightIntegrator
    IDirectLightIntegrator  <|-- RisDirectLightIntegrator
    PathIntegrator          --> IDirectLightIntegratorFactory
    IDirectLightIntegratorFactory ..> MisDirectLightIntegrator : creates
    IDirectLightIntegratorFactory ..> RisDirectLightIntegrator : creates
    MisDirectLightIntegrator --> ILightSampler
    RisDirectLightIntegrator --> ILightSampler
    ILightSampler <|-- UniformLightSampler
```

`PathTracePass` wires `PathIntegrator` + `MisDirectLightIntegrator` (MIS between BSDF and light strategies).
`RISPathTracePass` wires `PathIntegrator` + `RisDirectLightIntegrator` (candidate-weighted reservoir selection).

---

## Cross-frame reservoir: persistent buffer flow

The reservoir must survive across render calls so it can accumulate importance samples over many frames. Rather than making passes stateful, the pipeline owns a `PersistentStore` and injects named buffers into the render context before each pass execution, then extracts them afterward. Passes and integrators remain stateless.

```mermaid
sequenceDiagram
    participant P as RenderPipeline
    participant S as PersistentStore
    participant Pass as IntegrationPass
    participant I as RisDirectLightIntegrator

    P->>S: inject named buffers into RenderContext
    P->>Pass: Execute(ctx)
    Pass->>I: Li(isect, …, provider, callId)
    I->>S: read reservoir[pixel] via IBufferProvider
    I->>S: write updated reservoir[pixel]
    Pass-->>P: done
    P->>S: extract updated buffers from RenderContext
```

---

## Package dependency graph

Each `source/` subfolder is a **package** — an independent compilation unit intended to become its own shared library. Dependencies must flow strictly downward from volatile (top) to stable (bottom). Dashed edges are **violations**: cycles or upward dependencies that must be resolved before the packages can be split into separate DLLs.

```mermaid
graph TD
    subgraph L1["① Hydra adapter"]
        hydra
    end

    subgraph L2["② Renderer"]
        renderer
    end

    subgraph L3["③ Pipeline  ⚠ cycle between these two"]
        pipeline
        restir_pipeline
    end

    subgraph L4["④ Passes"]
        passes
        restir_passes
    end

    subgraph L5["⑤ Integration algorithms"]
        integrators
        restir
    end

    subgraph L6["⑥ Lighting"]
        lighting_core
    end

    subgraph L7["⑦ Scene & materials  ⚠ cycle between these two"]
        scene
        materials
        renderContext
    end

    subgraph L8["⑧ Interfaces"]
        sceneInterface
        rendererInterface
    end

    subgraph L9["⑨ Core  ⚠ cycle with sceneInterface"]
        core
        math
    end

    %% ── clean downward edges ─────────────────────────────────────────────
    hydra            --> renderer
    hydra            --> scene
    hydra            --> materials
    hydra            --> sceneInterface
    hydra            --> rendererInterface
    hydra            --> core

    renderer         --> renderContext
    renderer         --> scene
    renderer         --> sceneInterface
    renderer         --> rendererInterface
    renderer         --> core

    restir_pipeline  --> restir_passes
    restir_pipeline  --> passes

    restir_passes    --> restir
    restir_passes    --> integrators
    restir_passes    --> passes

    restir           --> integrators
    restir           --> lighting_core
    restir           --> materials
    restir           --> sceneInterface
    restir           --> core

    integrators      --> lighting_core
    integrators      --> materials
    integrators      --> sceneInterface
    integrators      --> core

    lighting_core    --> sceneInterface
    lighting_core    --> core

    scene            --> materials
    scene            --> sceneInterface
    scene            --> rendererInterface
    scene            --> core

    materials        --> core

    renderContext    --> sceneInterface
    renderContext    --> core

    rendererInterface --> core

    sceneInterface   --> core
    sceneInterface   --> math

    %% ── violation edges ──────────────────────────────────────────────────
    renderer        -.-> pipeline
    pipeline        -.-> renderer

    passes          -.-> pipeline
    pipeline        --> passes

    pipeline        -.-> restir_pipeline
    restir_pipeline -.-> pipeline

    renderer        -.-> passes
    passes          -.-> restir

    materials       -.-> scene
    scene           -.-> materials

    core            -.-> sceneInterface
    sceneInterface  -.-> renderContext
    renderContext   -.-> scene
    scene           -.-> sceneInterface
```

### Violation summary

| Packages                                                         | Cause                                                                                                                                                        | Fix                                                                                         |
| ---------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------- |
| `renderer` ↔ `pipeline`                                     | `restir_render_settings.h` is in `renderer` but pipeline compilation needs it; `renderer_pipeline_state.h` is in `pipeline` but renderer includes it | Move render-settings tokens into a shared `settings` package below both                   |
| `passes` ↔ `pipeline`                                       | `RenderPipeline` lives in `pipeline` but some pass headers include it                                                                                    | Move `render_pipeline.h` down into `passes`                                             |
| `pipeline` ↔ `restir_pipeline`                              | Each includes the other's pipeline factory header                                                                                                            | Merge `restir_pipeline` into `pipeline`                                                 |
| `renderer` → `passes`                                       | `Renderer` directly includes `path_trace_pass.h` for its settings struct                                                                                 | Extract `PathTracePassSettings` into `rendererInterface`                                |
| `passes` → `restir`                                         | Name collision between `passes/output_names.h` and `restir/output_names.h`                                                                               | Consolidate into `passes` only                                                            |
| `materials` ↔ `scene`                                       | `image_texture_sampler.h` lives in `scene` but materials uses it                                                                                         | Move `image_texture_sampler` into `materials`                                           |
| `core` ↔ `sceneInterface` ↔ `renderContext` ↔ `scene` | Scene interface types and render context are interleaved with core types across four packages                                                                | Merge `sceneInterface` into `core`; make `renderContext` a clean consumer of `core` |
