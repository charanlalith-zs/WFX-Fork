#ifndef WFX_SHARED_DEFERRED_INIT_VECTOR_HPP
#define WFX_SHARED_DEFERRED_INIT_VECTOR_HPP

#include <vector>

namespace WFX::Shared {

using DeferredCallback = void(*)();
using DeferredVector   = std::vector<DeferredCallback>;

inline DeferredVector& __WFXDeferredConstructors()
{
    static DeferredVector constructorsReg;
    return constructorsReg;
}

inline DeferredVector& __WFXDeferredRoutes()
{
    static DeferredVector routesReg;
    return routesReg;
}

inline DeferredVector& __WFXDeferredMiddleware()
{
    static DeferredVector middlewareReg;
    return middlewareReg;
}

inline void __EraseDeferredVector(DeferredVector& deferredVector)
{
    deferredVector.clear();
    deferredVector.shrink_to_fit();
}

} // namespace WFX::Shared

#endif // WFX_SHARED_DEFERRED_INIT_VECTOR_HPP