#ifndef WFX_SHARED_DEFERRED_INIT_VECTOR_HPP
#define WFX_SHARED_DEFERRED_INIT_VECTOR_HPP

#include <vector>

namespace WFX::Shared {

using DeferredCallback = void(*)();
using DeferredVector   = std::vector<DeferredCallback>;

// Inline variables so they can be same across all translation units
inline DeferredVector __WFXDeferredConstructors;
inline DeferredVector __WFXDeferredRoutes;
inline DeferredVector __WFXDeferredMiddleware;

inline void __EraseDeferredVector(DeferredVector& deferredVector)
{
    deferredVector.clear();
    deferredVector.shrink_to_fit();
}

} // namespace WFX::Shared

#endif // WFX_SHARED_DEFERRED_INIT_VECTOR_HPP