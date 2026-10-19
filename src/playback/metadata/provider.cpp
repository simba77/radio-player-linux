#include "metadata/provider.hpp"

#include "metadata/hls_daterange.hpp"
#include "metadata/icy.hpp"

namespace radio {

MetadataProvider *makeProvider(const Station &station, MpvBackend *mpv, QObject *parent)
{
    if (station.isHls())
        return new HlsDaterangeProvider(parent);
    return new IcyProvider(mpv, parent);
}

}  // namespace radio
