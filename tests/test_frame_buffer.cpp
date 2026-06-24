#include "frame_buffer_map.h"

#include <cassert>

namespace
{

    struct HitRecordStub
    {
        float values[4];
    };

    void TestAddAndGet()
    {
        Restir::FrameBuffersMap registry{};
        registry.Add("gbuf", sizeof(HitRecordStub), 100);

        const auto &span{registry.Get<HitRecordStub>("gbuf")};
        assert(span.size() == 100);
    }

    void TestReleaseOwned()
    {
        Restir::FrameBuffersMap registry;
        registry.Add("gbuf", sizeof(HitRecordStub), 8);
        registry.Add("acc", sizeof(HitRecordStub), 16);

        assert(registry.Has("gbuf"));
        assert(registry.Has("acc"));

        registry.Clear();

        assert(!registry.Has("gbuf"));
        assert(!registry.Has("acc"));
    }

} // namespace

int main()
{
    TestAddAndGet();
    TestReleaseOwned();
    return 0;
}
