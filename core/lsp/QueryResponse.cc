#include "core/lsp/QueryResponse.h"
#include "core/GlobalState.h"
template class std::unique_ptr<sorbet::core::QueryResponse>;

using namespace std;
namespace sorbet {
namespace core {

void QueryResponse::setQueryResponse(core::Context ctx, core::QueryResponse::Kind kind,
                                     core::DispatchResult::ComponentVec dispatchComponents,
                                     const shared_ptr<core::TypeConstraint> &constraint, core::Loc termLoc,
                                     core::NameRef name, core::TypeAndOrigins receiver, core::TypeAndOrigins retType) {
    auto queryResponse = make_unique<core::QueryResponse>();
    queryResponse->kind = kind;
    queryResponse->dispatchComponents = move(dispatchComponents);
    queryResponse->constraint = constraint;
    queryResponse->termLoc = termLoc;
    queryResponse->retType = retType;
    queryResponse->receiver = receiver;
    queryResponse->name = name;

    ctx.state.errorQueue->pushQueryResponse(move(queryResponse));
}

} // namespace core
} // namespace sorbet
