#include "common/common.h"
#include "core/Types.h"
#include <algorithm> // find_if
#include <unordered_map>

namespace ruby_typer {
namespace core {

using namespace std;

shared_ptr<core::Type> lubGround(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2);

shared_ptr<core::Type> core::Types::lub(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    auto ret = _lub(ctx, t1, t2);
    ENFORCE(Types::isSubType(ctx, t1, ret), ret->toString(ctx) + " is not a super type of " + t1->toString(ctx) +
                                                " was lubbing with " + t2->toString(ctx));
    ENFORCE(Types::isSubType(ctx, t2, ret), ret->toString(ctx) + " is not a super type of " + t2->toString(ctx) +
                                                " was lubbing with " + t1->toString(ctx));

    //  TODO: @dmitry, reenable
    //    ENFORCE(t1->hasUntyped() || t2->hasUntyped() || ret->hasUntyped() || // check if this test makes sense
    //                !Types::isSubType(ctx, t2, t1) || ret == t1 || ret->isDynamic(),
    //            "we do pointer comparisons in order to see if one is subtype of another. " + t1->toString(ctx) +
    //
    //                " was lubbing with " + t2->toString(ctx) + " got " + ret->toString(ctx));
    //
    //    ENFORCE(t1->hasUntyped() || t2->hasUntyped() || ret->hasUntyped() || // check if this test makes sense!
    //                !Types::isSubType(ctx, t1, t2) || ret == t2 || ret->isDynamic() || ret == t1 ||
    //                Types::isSubType(ctx, t2, t1),
    //            "we do pointer comparisons in order to see if one is subtype of another " + t1->toString(ctx) +
    //                " was lubbing with " + t2->toString(ctx) + " got " + ret->toString(ctx));

    ret->sanityCheck(ctx);

    return ret;
}

shared_ptr<core::Type> underlying(shared_ptr<Type> t1) {
    if (auto *f = cast_type<ProxyType>(t1.get())) {
        return f->underlying;
    }
    return t1;
}

shared_ptr<core::Type> lubDistributeOr(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    OrType *o1 = cast_type<OrType>(t1.get());
    ENFORCE(o1 != nullptr);
    shared_ptr<core::Type> n1 = Types::lub(ctx, o1->left, t2);
    if (n1.get() == o1->left.get()) {
        core::categoryCounterInc("lub_distribute_or.outcome", "t1");
        return t1;
    }
    shared_ptr<core::Type> n2 = Types::lub(ctx, o1->right, t2);
    if (n1.get() == t2.get()) {
        core::categoryCounterInc("lub_distribute_or.outcome", "n2'");
        return n2;
    }
    if (n2.get() == o1->right.get()) {
        core::categoryCounterInc("lub_distribute_or.outcome", "t1'");
        return t1;
    }
    if (n2.get() == t2.get()) {
        core::categoryCounterInc("lub_distribute_or.outcome", "n1'");
        return n1;
    }
    if (Types::isSubTypeWhenFrozen(ctx, n1, n2)) {
        core::categoryCounterInc("lub_distribute_or.outcome", "n2''");
        return n2;
    } else if (Types::isSubTypeWhenFrozen(ctx, n2, n1)) {
        core::categoryCounterInc("lub_distribute_or.outcome", "n1'''");
        return n1;
    }
    core::categoryCounterInc("lub_distribute_or.outcome", "worst");
    return OrType::make_shared(t1, underlying(t2)); // order matters for perf
}

shared_ptr<core::Type> glbDistributeAnd(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    AndType *a1 = cast_type<AndType>(t1.get());
    ENFORCE(t1 != nullptr);
    shared_ptr<core::Type> n1 = Types::glb(ctx, a1->left, t2);
    if (n1.get() == a1->left.get()) {
        core::categoryCounterInc("lub_distribute_or.outcome", "t1");
        return t1;
    }
    shared_ptr<core::Type> n2 = Types::glb(ctx, a1->right, t2);
    if (n1.get() == t2.get()) {
        core::categoryCounterInc("glbDistributeAnd.outcome", "Zn2");
        return n2;
    }
    if (n2.get() == a1->right.get()) {
        core::categoryCounterInc("glbDistributeAnd.outcome", "Zt1");
        return t1;
    }
    if (n2.get() == t2.get()) {
        core::categoryCounterInc("glbDistributeAnd.outcome", "Zn1");
        return n1;
    }
    if (Types::isSubTypeWhenFrozen(ctx, n1, n2)) {
        core::categoryCounterInc("glbDistributeAnd.outcome", "ZZn2");
        return n2;
    } else if (Types::isSubTypeWhenFrozen(ctx, n2, n1)) {
        core::categoryCounterInc("glbDistributeAnd.outcome", "ZZZn1");
        return n1;
    }

    core::categoryCounterInc("glbDistributeAnd.outcome", "worst");
    return AndType::make_shared(t1, t2);
}

// only keep knowledge in t1 that is not already present in t2. Return the same reference if unchaged
shared_ptr<core::Type> dropLubComponents(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    if (AndType *a1 = cast_type<AndType>(t1.get())) {
        auto a1a = dropLubComponents(ctx, a1->left, t2);
        auto a1b = dropLubComponents(ctx, a1->right, t2);
        auto subl = Types::isSubTypeWhenFrozen(ctx, a1a, t2);
        auto subr = Types::isSubTypeWhenFrozen(ctx, a1b, t2);
        if (subl || subr) {
            return Types::bottom();
        }
        if (a1a != a1->left || a1b != a1->right) {
            return Types::buildAnd(ctx, a1a, a1b);
        }
    } else if (OrType *o1 = cast_type<OrType>(t1.get())) {
        auto subl = Types::isSubTypeWhenFrozen(ctx, o1->left, t2);
        auto subr = Types::isSubTypeWhenFrozen(ctx, o1->right, t2);
        if (subl && subr) {
            return Types::bottom();
        } else if (subl) {
            return o1->right;
        } else if (subr) {
            return o1->left;
        }
    }
    return t1;
}

shared_ptr<core::Type> core::Types::_lub(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    if (t1.get() == t2.get()) {
        core::categoryCounterInc("lub", "ref-eq");
        return t1;
    }

    if (t1->kind() > t2->kind()) { // force the relation to be symmentric and half the implementation
        return _lub(ctx, t2, t1);
    }

    if (ClassType *mayBeSpecial1 = cast_type<ClassType>(t1.get())) {
        if (mayBeSpecial1->symbol == core::Symbols::untyped()) {
            core::categoryCounterInc("lub", "<untyped");
            return t1;
        }
        if (mayBeSpecial1->symbol == core::Symbols::bottom()) {
            core::categoryCounterInc("lub", "<bottom");
            return t2;
        }
        if (mayBeSpecial1->symbol == core::Symbols::top()) {
            core::categoryCounterInc("lub", "<top");
            return t1;
        }
    }

    if (ClassType *mayBeSpecial2 = cast_type<ClassType>(t2.get())) {
        if (mayBeSpecial2->symbol == core::Symbols::untyped()) {
            core::categoryCounterInc("lub", "untyped>");
            return t2;
        }
        if (mayBeSpecial2->symbol == core::Symbols::bottom()) {
            core::categoryCounterInc("lub", "bottom>");
            return t1;
        }
        if (mayBeSpecial2->symbol == core::Symbols::top()) {
            core::categoryCounterInc("lub", "top>");
            return t2;
        }
    }

    if (auto *o2 = cast_type<OrType>(t2.get())) { // 3, 5, 6
        core::categoryCounterInc("lub", "or>");
        return lubDistributeOr(ctx, t2, t1);
    } else if (auto *a2 = cast_type<AndType>(t2.get())) { // 2, 4
        core::categoryCounterInc("lub", "and>");
        auto t1d = underlying(t1);
        auto t2filtered = dropLubComponents(ctx, t2, t1d);
        if (t2filtered != t2) {
            return lub(ctx, t1, t2filtered);
        }
        return OrType::make_shared(t1, t2filtered);
    } else if (cast_type<OrType>(t1.get()) != nullptr) {
        core::categoryCounterInc("lub", "<or");
        return lubDistributeOr(ctx, t1, t2);
    }

    if (AppliedType *a1 = cast_type<AppliedType>(t1.get())) {
        AppliedType *a2 = cast_type<AppliedType>(t2.get());
        if (a2 == nullptr) {
            if (isSubType(ctx, t2, t1)) {
                return t1;
            }
            return OrType::make_shared(t1, t2);
        }

        bool ltr = a1->klass == a2->klass || a2->klass.data(ctx).derivesFrom(ctx, a1->klass);
        bool rtl = !ltr && a1->klass.data(ctx).derivesFrom(ctx, a2->klass);
        if (!rtl && !ltr) {
            return OrType::make_shared(t1, t2);
        }
        if (ltr) {
            std::swap(a1, a2);
            std::swap(t1, t2);
        }
        // now a1 <: a2

        vector<SymbolRef> indexes = Types::alignBaseTypeArgs(ctx, a1->klass, a1->targs, a2->klass);
        vector<shared_ptr<Type>> newTargs;
        newTargs.reserve(indexes.size());
        // code below inverts permutation of type params
        int j = 0;
        bool changed = false;
        for (SymbolRef idx : a2->klass.data(ctx).typeMembers()) {
            int i = 0;
            while (indexes[j] != a1->klass.data(ctx).typeMembers()[i]) {
                i++;
            }
            ENFORCE(i < a1->klass.data(ctx).typeMembers().size());
            if (idx.data(ctx).isCovariant()) {
                newTargs.push_back(Types::lub(ctx, a1->targs[i], a2->targs[j]));
            } else if (idx.data(ctx).isInvariant()) {
                if (!Types::equiv(ctx, a1->targs[i], a2->targs[j])) {
                    return OrType::make_shared(t1, t2);
                }
                if (a1->targs[i]->isDynamic()) {
                    newTargs.push_back(a1->targs[i]);
                } else {
                    newTargs.push_back(a2->targs[j]);
                }

            } else if (idx.data(ctx).isContravariant()) {
                newTargs.push_back(Types::glb(ctx, a1->targs[i], a2->targs[j]));
            }
            changed = changed || newTargs.back() != a2->targs[j];
            j++;
        }
        if (changed) {
            return make_shared<AppliedType>(a2->klass, newTargs);
        } else {
            return t2;
        }
    }

    if (ProxyType *p1 = cast_type<ProxyType>(t1.get())) {
        core::categoryCounterInc("lub", "<proxy");
        if (ProxyType *p2 = cast_type<ProxyType>(t2.get())) {
            core::categoryCounterInc("lub", "proxy>");
            // both are proxy
            shared_ptr<core::Type> result;
            typecase(
                p1,
                [&](TupleType *a1) { // Warning: this implements COVARIANT arrays
                    if (TupleType *a2 = cast_type<TupleType>(p2)) {
                        if (a1->elems.size() == a2->elems.size()) { // lub arrays only if they have same element count
                            vector<shared_ptr<core::Type>> elemLubs;
                            int i = -1;
                            bool differ1 = false;
                            bool differ2 = false;
                            for (auto &el2 : a2->elems) {
                                ++i;
                                elemLubs.emplace_back(lub(ctx, a1->elems[i], el2));
                                differ1 = differ1 || elemLubs.back() != a1->elems[i];
                                differ2 = differ2 || elemLubs.back() != el2;
                            }
                            if (!differ1) {
                                result = t1;
                            } else if (!differ2) {
                                result = t2;
                            } else {
                                result = make_shared<TupleType>(elemLubs);
                            }
                        } else {
                            result = core::Types::arrayOfUntyped();
                        }
                    } else {
                        result = lub(ctx, p1->underlying, p2->underlying);
                    }
                },
                [&](ShapeType *h1) { // Warning: this implements COVARIANT hashes
                    if (ShapeType *h2 = cast_type<ShapeType>(p2)) {
                        if (h2->keys.size() == h1->keys.size()) {
                            // have enough keys.
                            int i = -1;
                            vector<shared_ptr<core::LiteralType>> keys;
                            vector<shared_ptr<core::Type>> valueLubs;
                            bool differ1 = false;
                            bool differ2 = false;
                            for (auto &el2 : h2->keys) {
                                ++i;
                                ClassType *u2 = cast_type<ClassType>(el2->underlying.get());
                                ENFORCE(u2 != nullptr);
                                auto fnd = find_if(h1->keys.begin(), h1->keys.end(), [&](auto &candidate) -> bool {
                                    ClassType *u1 = cast_type<ClassType>(candidate->underlying.get());
                                    return candidate->value == el2->value && u1 == u2; // from lambda
                                });
                                if (fnd != h1->keys.end()) {
                                    keys.emplace_back(el2);
                                    valueLubs.emplace_back(lub(ctx, h1->values[fnd - h1->keys.begin()], h2->values[i]));
                                    differ1 = differ1 || valueLubs.back() != h1->values[fnd - h1->keys.begin()];
                                    differ2 = differ2 || valueLubs.back() != h2->values[i];
                                } else {
                                    result = core::Types::hashOfUntyped();
                                    return;
                                }
                            }
                            if (!differ1) {
                                result = t1;
                            } else if (!differ2) {
                                result = t2;
                            } else {
                                result = make_shared<ShapeType>(keys, valueLubs);
                            }
                        } else {
                            result = core::Types::hashOfUntyped();
                        }
                    } else {
                        result = lub(ctx, p1->underlying, p2->underlying);
                    }
                },
                [&](LiteralType *l1) {
                    if (LiteralType *l2 = cast_type<LiteralType>(p2)) {
                        ClassType *u1 = cast_type<ClassType>(l1->underlying.get());
                        ClassType *u2 = cast_type<ClassType>(l2->underlying.get());
                        ENFORCE(u1 != nullptr && u2 != nullptr);
                        if (u1->symbol == u2->symbol) {
                            if (l1->value == l2->value) {
                                result = t1;
                            } else {
                                result = l1->underlying;
                            }
                        } else {
                            result = lubGround(ctx, l1->underlying, l2->underlying);
                        }
                    } else {
                        result = lub(ctx, p1->underlying, p2->underlying);
                    }
                });
            ENFORCE(result.get() != nullptr);
            return result;
        } else {
            // only 1st is proxy
            shared_ptr<Type> und = p1->underlying;
            return lub(ctx, und, t2);
        }
    } else if (ProxyType *p2 = cast_type<ProxyType>(t2.get())) {
        core::categoryCounterInc("lub", "proxy>");
        // only 2nd is proxy
        shared_ptr<Type> und = p2->underlying;
        return lub(ctx, t1, und);
    }

    LambdaParam *p1 = cast_type<LambdaParam>(t1.get());
    LambdaParam *p2 = cast_type<LambdaParam>(t2.get());

    if (p1 != nullptr || p2 != nullptr) {
        return OrType::make_shared(t1, t2);
    }

    SelfTypeParam *s1 = cast_type<SelfTypeParam>(t1.get());
    SelfTypeParam *s2 = cast_type<SelfTypeParam>(t2.get());

    if (s1 != nullptr || s2 != nullptr) {
        if (s1 == nullptr || s2 == nullptr || s2->definition != s1->definition) {
            return OrType::make_shared(t1, t2);
        } else {
            return t1;
        }
    }

    // none is proxy
    return lubGround(ctx, t1, t2);
}

shared_ptr<core::Type> lubGround(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    auto *g1 = cast_type<GroundType>(t1.get());
    auto *g2 = cast_type<GroundType>(t2.get());
    ENFORCE(g1 != nullptr);
    ENFORCE(g2 != nullptr);

    //    if (g1->kind() > g2->kind()) { // force the relation to be symmentric and half the implementation
    //        return lubGround(ctx, t2, t1);
    //    }
    /** this implementation makes a bet that types are small and very likely to be collapsable.
     * The more complex types we have, the more likely this bet is to be wrong.
     */
    if (t1.get() == t2.get()) {
        core::categoryCounterInc("lub", "ref-eq2");
        return t1;
    }

    // Prereq: t1.kind <= t2.kind
    // pairs to cover: 1  (Class, Class)
    //                 2  (Class, And)
    //                 3  (Class, Or)
    //                 4  (And, And)
    //                 5  (And, Or)
    //                 6  (Or, Or)

    shared_ptr<core::Type> result;

    // 1 :-)
    ClassType *c1 = cast_type<ClassType>(t1.get());
    ClassType *c2 = cast_type<ClassType>(t2.get());
    core::categoryCounterInc("lub", "<class>");
    ENFORCE(c1 != nullptr && c2 != nullptr);

    core::SymbolRef sym1 = c1->symbol;
    core::SymbolRef sym2 = c2->symbol;
    if (sym1 == sym2 || sym2.data(ctx).derivesFrom(ctx, sym1)) {
        core::categoryCounterInc("lub.<class>.collapsed", "yes");
        return t1;
    } else if (sym1.data(ctx).derivesFrom(ctx, sym2)) {
        core::categoryCounterInc("lub.<class>.collapsed", "yes");
        return t2;
    } else {
        core::categoryCounterInc("lub.class>.collapsed", "no");
        return OrType::make_shared(t1, t2);
    }
}

shared_ptr<core::Type> glbGround(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    auto *g1 = cast_type<GroundType>(t1.get());
    auto *g2 = cast_type<GroundType>(t2.get());
    ENFORCE(g1 != nullptr);
    ENFORCE(g2 != nullptr);

    if (g1->kind() > g2->kind()) { // force the relation to be symmentric and half the implementation
        return glbGround(ctx, t2, t1);
    }
    /** this implementation makes a bet that types are small and very likely to be collapsable.
     * The more complex types we have, the more likely this bet is to be wrong.
     */
    if (t1.get() == t2.get()) {
        core::categoryCounterInc("glb", "ref-eq2");
        return t1;
    }

    // Prereq: t1.kind <= t2.kind
    // pairs to cover: 1  (Class, Class)
    //                 2  (Class, And)
    //                 3  (Class, Or)
    //                 4  (And, And)
    //                 5  (And, Or)
    //                 6  (Or, Or)

    shared_ptr<core::Type> result;
    // 1 :-)
    ClassType *c1 = cast_type<ClassType>(t1.get());
    ClassType *c2 = cast_type<ClassType>(t2.get());
    ENFORCE(c1 != nullptr && c2 != nullptr);
    core::categoryCounterInc("glb", "<class>");

    core::SymbolRef sym1 = c1->symbol;
    core::SymbolRef sym2 = c2->symbol;
    if (sym1 == sym2 || sym1.data(ctx).derivesFrom(ctx, sym2)) {
        core::categoryCounterInc("glb.<class>.collapsed", "yes");
        return t1;
    } else if (sym2.data(ctx).derivesFrom(ctx, sym1)) {
        core::categoryCounterInc("glb.<class>.collapsed", "yes");
        return t2;
    } else {
        if (sym1.data(ctx).isClassClass() && sym2.data(ctx).isClassClass()) {
            core::categoryCounterInc("glb.<class>.collapsed", "bottom");
            return Types::bottom();
        }
        core::categoryCounterInc("glb.<class>.collapsed", "no");
        return AndType::make_shared(t1, t2);
    }
}
shared_ptr<core::Type> core::Types::glb(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    auto ret = _glb(ctx, t1, t2);
    ret->sanityCheck(ctx);

    ENFORCE(Types::isSubType(ctx, ret, t1), ret->toString(ctx) + " is not a subtype of " + t1->toString(ctx) +
                                                " was glbbing with " + t2->toString(ctx));

    ENFORCE(Types::isSubType(ctx, ret, t2), ret->toString(ctx) + " is not a subtype of " + t2->toString(ctx) +
                                                " was glbbing with " + t1->toString(ctx));
    //  TODO: @dmitry, reenable
    //    ENFORCE(t1->hasUntyped() || t2->hasUntyped() || ret->hasUntyped() || // check if this test makes sense
    //                !Types::isSubType(ctx, t1, t2) || ret == t1 || ret->isDynamic(),
    //            "we do pointer comparisons in order to see if one is subtype of another. " + t1->toString(ctx) +
    //
    //                " was glbbing with " + t2->toString(ctx) + " got " + ret->toString(ctx));
    //
    //    ENFORCE(t1->hasUntyped() || t2->hasUntyped() || ret->hasUntyped() || // check if this test makes sense
    //                !Types::isSubType(ctx, t2, t1) || ret == t2 || ret->isDynamic() || ret == t1 ||
    //                Types::isSubType(ctx, t1, t2),
    //            "we do pointer comparisons in order to see if one is subtype of another " + t1->toString(ctx) +
    //                " was glbbing with " + t2->toString(ctx) + " got " + ret->toString(ctx));

    return ret;
}

shared_ptr<core::Type> core::Types::_glb(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    if (t1.get() == t2.get()) {
        core::categoryCounterInc("glb", "ref-eq");
        return t1;
    }

    if (ClassType *mayBeSpecial1 = cast_type<ClassType>(t1.get())) {
        if (mayBeSpecial1->symbol == core::Symbols::untyped()) {
            core::categoryCounterInc("glb", "<untyped");
            return t1;
        }
        if (mayBeSpecial1->symbol == core::Symbols::bottom()) {
            core::categoryCounterInc("glb", "<bottom");
            return t1;
        }
        if (mayBeSpecial1->symbol == core::Symbols::top()) {
            core::categoryCounterInc("glb", "<top");
            return t2;
        }
    }

    if (ClassType *mayBeSpecial2 = cast_type<ClassType>(t2.get())) {
        if (mayBeSpecial2->symbol == core::Symbols::untyped()) {
            core::categoryCounterInc("glb", "untyped>");
            return t2;
        }
        if (mayBeSpecial2->symbol == core::Symbols::bottom()) {
            core::categoryCounterInc("glb", "bottom>");
            return t2;
        }
        if (mayBeSpecial2->symbol == core::Symbols::top()) {
            core::categoryCounterInc("glb", "top>");
            return t1;
        }
    }

    if (t1->kind() > t2->kind()) { // force the relation to be symmentric and half the implementation
        return _glb(ctx, t2, t1);
    }
    if (auto *a1 = cast_type<AndType>(t1.get())) { // 4, 5
        core::categoryCounterInc("glb", "<and");
        return glbDistributeAnd(ctx, t1, t2);
    } else if (auto *a2 = cast_type<AndType>(t2.get())) { // 2
        core::categoryCounterInc("glb", "and>");
        return glbDistributeAnd(ctx, t2, t1);
    }

    if (ProxyType *p1 = cast_type<ProxyType>(t1.get())) {
        if (ProxyType *p2 = cast_type<ProxyType>(t2.get())) {
            if (typeid(*p1) != typeid(*p2)) {
                return Types::bottom();
            }
            shared_ptr<core::Type> result;
            typecase(p1,
                     [&](TupleType *a1) { // Warning: this implements COVARIANT arrays
                         TupleType *a2 = cast_type<TupleType>(p2);
                         ENFORCE(a2 != nullptr);
                         if (a1->elems.size() == a2->elems.size()) { // lub arrays only if they have same element count
                             vector<shared_ptr<core::Type>> elemGlbs;
                             int i = -1;
                             for (auto &el2 : a2->elems) {
                                 ++i;
                                 auto glbe = glb(ctx, a1->elems[i], el2);
                                 if (glbe->isBottom()) {
                                     result = Types::bottom();
                                     return;
                                 }
                                 elemGlbs.emplace_back(glbe);
                             }
                             result = make_shared<TupleType>(elemGlbs);
                         } else {
                             result = Types::bottom();
                         }

                     },
                     [&](ShapeType *h1) { // Warning: this implements COVARIANT hashes
                         ShapeType *h2 = cast_type<ShapeType>(p2);
                         ENFORCE(h2 != nullptr);
                         if (h2->keys.size() == h1->keys.size()) {
                             // have enough keys.
                             int i = -1;
                             vector<shared_ptr<core::LiteralType>> keys;
                             vector<shared_ptr<core::Type>> valueLubs;
                             for (auto &el2 : h2->keys) {
                                 ++i;
                                 ClassType *u2 = cast_type<ClassType>(el2->underlying.get());
                                 ENFORCE(u2 != nullptr);
                                 auto fnd = find_if(h1->keys.begin(), h1->keys.end(), [&](auto &candidate) -> bool {
                                     ClassType *u1 = cast_type<ClassType>(candidate->underlying.get());
                                     return candidate->value == el2->value && u1 == u2; // from lambda
                                 });
                                 if (fnd != h1->keys.end()) {
                                     keys.emplace_back(el2);
                                     auto glbe = glb(ctx, h1->values[fnd - h1->keys.begin()], h2->values[i]);
                                     if (glbe->isBottom()) {
                                         result = Types::bottom();
                                         return;
                                     }
                                     valueLubs.emplace_back(glbe);
                                 } else {
                                     result = Types::bottom();
                                     return;
                                 }
                             }
                             result = make_shared<ShapeType>(keys, valueLubs);
                         } else {
                             result = Types::bottom();
                         }

                     },
                     [&](LiteralType *l1) {
                         LiteralType *l2 = cast_type<LiteralType>(p2);
                         ENFORCE(l2 != nullptr);
                         ClassType *u1 = cast_type<ClassType>(l1->underlying.get());
                         ClassType *u2 = cast_type<ClassType>(l2->underlying.get());
                         ENFORCE(u1 != nullptr && u2 != nullptr);
                         if (u1->symbol == u2->symbol) {
                             if (l1->value == l2->value) {
                                 result = t1;
                             } else {
                                 result = Types::bottom();
                             }
                         } else {
                             result = Types::bottom();
                         }
                     });
            ENFORCE(result.get() != nullptr);
            return result;
        } else {
            // only 1st is proxy
            if (Types::isSubTypeWhenFrozen(ctx, t1, t2)) {
                return t1;
            } else {
                return Types::bottom();
            }
        }
    } else if (ProxyType *p2 = cast_type<ProxyType>(t2.get())) {
        // only 1st is proxy
        if (Types::isSubTypeWhenFrozen(ctx, t2, t1)) {
            return t2;
        } else {
            return Types::bottom();
        }
    }

    if (auto *o2 = cast_type<OrType>(t2.get())) { // 3, 6
        bool collapseInLeft = Types::isSubTypeWhenFrozen(ctx, t1, t2);
        if (collapseInLeft) {
            core::categoryCounterInc("glb", "Zor");
            return t1;
        }

        bool collapseInRight = Types::isSubTypeWhenFrozen(ctx, t2, t1);
        if (collapseInRight) {
            core::categoryCounterInc("glb", "ZZor");
            return t2;
        }

        if (auto *c1 = cast_type<ClassType>(t1.get())) {
            auto lft = Types::glb(ctx, t1, o2->left);
            if (Types::isSubTypeWhenFrozen(ctx, lft, o2->right)) {
                core::categoryCounterInc("glb", "ZZZorClass");
                return lft;
            }
            auto rght = Types::glb(ctx, t1, o2->right);
            if (Types::isSubTypeWhenFrozen(ctx, rght, o2->left)) {
                core::categoryCounterInc("glb", "ZZZZorClass");
                return rght;
            }
            if (lft->isBottom()) {
                return rght;
            }
            if (rght->isBottom()) {
                return lft;
            }
        }

        if (auto *o1 = cast_type<OrType>(t1.get())) { // 6
            auto t11 = Types::glb(ctx, o1->left, o2->left);
            auto t12 = Types::glb(ctx, o1->left, o2->right);
            auto t21 = Types::glb(ctx, o1->right, o2->left);
            auto t22 = Types::glb(ctx, o1->right, o2->right);

            // This is a heuristic to try and eagerly make a smaller type. For
            // now we are choosing that if any type collapses then we should use
            // an Or otherwise use an And.
            auto score = 0;
            if (t11 == o1->left || t11 == o2->left) {
                score++;
            }
            if (t12 == o1->left || t12 == o2->right) {
                score++;
            }
            if (t21 == o1->right || t21 == o2->left) {
                score++;
            }
            if (t22 == o1->right || t22 == o2->right) {
                score++;
            }
            if (t11->isBottom() || t12->isBottom() || t21->isBottom() || t22->isBottom()) {
                score++;
            }

            if (score > 0) {
                return Types::lub(ctx, Types::lub(ctx, t11, t12), Types::lub(ctx, t21, t22));
            }
        }
        core::categoryCounterInc("glb.orcollapsed", "no");
        return AndType::make_shared(t1, t2);
    }

    if (AppliedType *a1 = cast_type<AppliedType>(t1.get())) {
        AppliedType *a2 = cast_type<AppliedType>(t2.get());
        if (a2 == nullptr) {
            return AndType::make_shared(t1, t2);
        }
        bool rtl = a1->klass == a2->klass || a1->klass.data(ctx).derivesFrom(ctx, a2->klass);
        bool ltr = !rtl && a2->klass.data(ctx).derivesFrom(ctx, a1->klass);
        if (!rtl && !ltr) {
            return AndType::make_shared(t1, t2); // we can as well return nothing here?
        }
        if (ltr) { // swap
            std::swap(a1, a2);
        }
        // a1 <:< a2

        vector<SymbolRef> indexes = Types::alignBaseTypeArgs(ctx, a2->klass, a2->targs, a1->klass);

        // code below inverts permutation of type params

        vector<shared_ptr<Type>> newTargs;
        newTargs.reserve(a1->klass.data(ctx).typeMembers().size());
        int j = 0;
        for (SymbolRef idx : a1->klass.data(ctx).typeMembers()) {
            int i = 0;
            if (j >= indexes.size()) {
                i = INT_MAX;
            }
            while (i < a2->klass.data(ctx).typeMembers().size() && indexes[j] != a2->klass.data(ctx).typeMembers()[i]) {
                i++;
            }
            if (i >= a2->klass.data(ctx).typeMembers().size()) { // a1 has more tparams, this is fine, it's a child
                newTargs.push_back(a1->targs[j]);
            } else {
                if (idx.data(ctx).isCovariant()) {
                    newTargs.push_back(Types::glb(ctx, a1->targs[j], a2->targs[i]));
                } else if (idx.data(ctx).isInvariant()) {
                    if (!Types::equiv(ctx, a1->targs[j], a2->targs[i])) {
                        return AndType::make_shared(t1, t2);
                    }
                    if (a1->targs[j]->isDynamic()) {
                        newTargs.push_back(a2->targs[i]);
                    } else {
                        newTargs.push_back(a1->targs[j]);
                    }
                } else if (idx.data(ctx).isContravariant()) {
                    newTargs.push_back(Types::lub(ctx, a1->targs[j], a2->targs[i]));
                }
            }
            j++;
        }
        return make_shared<AppliedType>(a1->klass, newTargs);
    }

    SelfTypeParam *s1 = cast_type<SelfTypeParam>(t1.get());
    SelfTypeParam *s2 = cast_type<SelfTypeParam>(t2.get());

    if (s1 != nullptr || s2 != nullptr) {
        if (s1 == nullptr || s2 == nullptr || s2->definition != s1->definition) {
            return AndType::make_shared(t1, t2);
        } else {
            return t1;
        }
    }

    return glbGround(ctx, t1, t2);
}

bool classSymbolIsAsGoodAs(core::Context ctx, core::SymbolRef c1, core::SymbolRef c2) {
    ENFORCE(c1.data(ctx).isClass());
    ENFORCE(c2.data(ctx).isClass());
    return c1 == c2 || c1.data(ctx).derivesFrom(ctx, c2);
}

// "Single" means "ClassType or ProxyType"; since ProxyTypes are constrained to
// be proxies over class types, this means "class or class-like"
bool isSubTypeSingle(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    if (t1.get() == t2.get()) {
        return true;
    }
    if (ClassType *mayBeSpecial1 = cast_type<ClassType>(t1.get())) {
        if (mayBeSpecial1->symbol == core::Symbols::untyped()) {
            return true;
        }
        if (mayBeSpecial1->symbol == core::Symbols::bottom()) {
            return true;
        }
        if (mayBeSpecial1->symbol == core::Symbols::top()) {
            if (ClassType *mayBeSpecial2 = cast_type<ClassType>(t2.get())) {
                return mayBeSpecial2->symbol == core::Symbols::top();
            } else {
                return false;
            }
        }
    }

    if (ClassType *mayBeSpecial2 = cast_type<ClassType>(t2.get())) {
        if (mayBeSpecial2->symbol == core::Symbols::untyped()) {
            return true;
        }
        if (mayBeSpecial2->symbol == core::Symbols::bottom()) {
            return false; // (bot, bot) is handled above.
        }
        if (mayBeSpecial2->symbol == core::Symbols::top()) {
            return true;
        }
    }

    //    ENFORCE(cast_type<LambdaParam>(t1.get()) == nullptr); // sandly, this is false in Resolver, as we build
    //    original signatures using lub ENFORCE(cast_type<LambdaParam>(t2.get()) == nullptr);

    auto *lambda1 = cast_type<LambdaParam>(t1.get());
    auto *lambda2 = cast_type<LambdaParam>(t2.get());
    if (lambda1 != nullptr || lambda2 != nullptr) {
        // This should only be reachable in resolver.
        if (lambda1 == nullptr || lambda2 == nullptr) {
            return false;
        }
        return lambda1->definition == lambda2->definition;
    }

    auto *self1 = cast_type<SelfTypeParam>(t1.get());
    auto *self2 = cast_type<SelfTypeParam>(t2.get());
    if (self1 != nullptr || self2 != nullptr) {
        if (self1 == nullptr || self2 == nullptr) {
            return false;
        }
        return self1->definition == self2->definition;
    }

    if (AppliedType *a1 = cast_type<AppliedType>(t1.get())) {
        AppliedType *a2 = cast_type<AppliedType>(t2.get());
        bool result;
        if (a2 == nullptr) {
            if (ClassType *c2 = cast_type<ClassType>(t2.get())) {
                return classSymbolIsAsGoodAs(ctx, a1->klass, c2->symbol);
            }
            return false;
        } else {
            result = classSymbolIsAsGoodAs(ctx, a1->klass, a2->klass);
        }
        if (result) {
            vector<SymbolRef> indexes = Types::alignBaseTypeArgs(ctx, a1->klass, a1->targs, a2->klass);
            // code below inverts permutation of type params
            int j = 0;
            for (SymbolRef idx : a2->klass.data(ctx).typeMembers()) {
                int i = 0;
                while (indexes[j] != a1->klass.data(ctx).typeMembers()[i]) {
                    i++;
                }

                ENFORCE(i < a1->klass.data(ctx).typeMembers().size());

                if (idx.data(ctx).isCovariant()) {
                    result = Types::isSubType(ctx, a1->targs[i], a2->targs[j]);
                } else if (idx.data(ctx).isInvariant()) {
                    result = Types::equiv(ctx, a1->targs[i], a2->targs[j]);
                } else if (idx.data(ctx).isContravariant()) {
                    result = Types::isSubType(ctx, a2->targs[j], a1->targs[i]);
                }
                if (!result) {
                    break;
                }
                j++;
            }
            // alight type params.
        }
        return result;
    }
    if (AppliedType *a2 = cast_type<AppliedType>(t2.get())) {
        if (ProxyType *pt = cast_type<ProxyType>(t1.get())) {
            return Types::isSubType(ctx, pt->underlying, t2);
        }
        return false;
    }

    if (ProxyType *p1 = cast_type<ProxyType>(t1.get())) {
        if (ProxyType *p2 = cast_type<ProxyType>(t2.get())) {
            bool result;
            // TODO: simply compare as memory regions
            typecase(p1,
                     [&](TupleType *a1) { // Warning: this implements COVARIANT arrays
                         TupleType *a2 = cast_type<TupleType>(p2);
                         result = a2 != nullptr && a1->elems.size() >= a2->elems.size();
                         if (result) {
                             int i = -1;
                             for (auto &el2 : a2->elems) {
                                 ++i;
                                 result = Types::isSubType(ctx, a1->elems[i], el2);
                                 if (!result) {
                                     break;
                                 }
                             }
                         }
                     },
                     [&](ShapeType *h1) { // Warning: this implements COVARIANT hashes
                         ShapeType *h2 = cast_type<ShapeType>(p2);
                         result = h2 != nullptr && h2->keys.size() <= h1->keys.size();
                         if (!result) {
                             return;
                         }
                         // have enough keys.
                         int i = -1;
                         for (auto &el2 : h2->keys) {
                             ++i;
                             ClassType *u2 = cast_type<ClassType>(el2->underlying.get());
                             ENFORCE(u2 != nullptr);
                             auto fnd = find_if(h1->keys.begin(), h1->keys.end(), [&](auto &candidate) -> bool {
                                 ClassType *u1 = cast_type<ClassType>(candidate->underlying.get());
                                 return candidate->value == el2->value && u1 == u2; // from lambda
                             });
                             result = fnd != h1->keys.end() &&
                                      Types::isSubType(ctx, h1->values[fnd - h1->keys.begin()], h2->values[i]);
                             if (!result) {
                                 return;
                             }
                         }
                     },
                     [&](LiteralType *l1) {
                         LiteralType *l2 = cast_type<LiteralType>(p2);
                         ClassType *u1 = cast_type<ClassType>(l1->underlying.get());
                         ClassType *u2 = cast_type<ClassType>(l2->underlying.get());
                         ENFORCE(u1 != nullptr && u2 != nullptr);
                         result = l2 != nullptr && u1->symbol == u2->symbol && l1->value == l2->value;
                     });
            return result;
            // both are proxy
        } else {
            // only 1st is proxy
            shared_ptr<Type> und = p1->underlying;
            return isSubTypeSingle(ctx, und, t2);
        }
    } else if (ProxyType *p2 = cast_type<ProxyType>(t2.get())) {
        // non-proxies are never subtypes of proxies.
        return false;
    } else if (isa_type<MetaType>(t1.get()) || isa_type<MetaType>(t2.get())) {
        // MetaTypes are not a subclass of anything and nothing is a subclass of
        // them. Correct code should never reach this point, but erroneous code
        // can (e.g. `puts(T::Array[String])`).
        return false;
    } else {
        if (auto *c1 = cast_type<ClassType>(t1.get())) {
            if (auto *c2 = cast_type<ClassType>(t2.get())) {
                return classSymbolIsAsGoodAs(ctx, c1->symbol, c2->symbol);
            }
        }
        Error::raise("isSubType(", t1->typeName(), ", ", t2->typeName(), "): unreachable");
    }
}

bool core::Types::isSubType(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    if (t1.get() == t2.get()) {
        return true;
    }

    // pairs to cover: 1  (_, _)
    //                 2  (_, And)
    //                 3  (_, Or)
    //                 4  (And, _)
    //                 5  (And, And)
    //                 6  (And, Or)
    //                 7 (Or, _)
    //                 8 (Or, And)
    //                 9 (Or, Or)
    // _ wildcards are ClassType or ProxyType(ClassType)

    // Note: order of cases here matters!
    if (auto *o1 = cast_type<OrType>(t1.get())) { // 7, 8, 9
        return Types::isSubType(ctx, o1->left, t2) && Types::isSubType(ctx, o1->right, t2);
    }

    if (auto *a2 = cast_type<AndType>(t2.get())) { // 2, 5
        return Types::isSubType(ctx, t1, a2->left) && Types::isSubType(ctx, t1, a2->right);
    }

    auto *a1 = cast_type<AndType>(t1.get());
    auto *o2 = cast_type<OrType>(t2.get());

    if (a1 != nullptr) {
        // If the left is an And of an Or, then we can reorder it to be an Or of
        // an And, which lets us recurse on smaller types
        auto l = a1->left;
        auto r = a1->right;
        if (isa_type<OrType>(r.get())) {
            swap(r, l);
        }
        auto *a2o = cast_type<OrType>(l.get());
        if (a2o != nullptr) {
            // This handles `(A | B) & C` -> `(A & C) | (B & C)`
            return Types::isSubType(ctx, glb(ctx, a2o->left, r), t2) &&
                   Types::isSubType(ctx, glb(ctx, a2o->right, r), t2);
        }
    }
    if (o2 != nullptr) {
        // Simiarly to above, if the right is an Or of an And, then we can reorder it to be an And of
        // an Or, which lets us recurse on smaller types
        auto l = o2->left;
        auto r = o2->right;
        if (isa_type<AndType>(r.get())) {
            swap(r, l);
        }
        auto *o2a = cast_type<AndType>(l.get());
        if (o2a != nullptr) {
            // This handles `(A & B) | C` -> `(A | C) & (B | C)`
            return Types::isSubType(ctx, t1, lub(ctx, o2a->left, r)) &&
                   Types::isSubType(ctx, t1, lub(ctx, o2a->right, r));
        }
    }

    // This order matters
    if (o2 != nullptr) {
        return Types::isSubType(ctx, t1, o2->left) || Types::isSubType(ctx, t1, o2->right); // 3
    }
    if (a1 != nullptr) { // 4
        return Types::isSubType(ctx, a1->left, t2) || Types::isSubType(ctx, a1->right, t2);
    }

    return isSubTypeSingle(ctx, t1, t2); // 1
}

bool core::Types::equiv(core::Context ctx, shared_ptr<Type> t1, shared_ptr<Type> t2) {
    return isSubType(ctx, t1, t2) && isSubType(ctx, t2, t1);
}

bool ProxyType::derivesFrom(const core::GlobalState &gs, core::SymbolRef klass) {
    return underlying->derivesFrom(gs, klass);
}

bool ClassType::derivesFrom(const core::GlobalState &gs, core::SymbolRef klass) {
    if (symbol == core::Symbols::untyped() || symbol == klass) {
        return true;
    }
    return symbol.data(gs).derivesFrom(gs, klass);
}

bool OrType::derivesFrom(const core::GlobalState &gs, core::SymbolRef klass) {
    return left->derivesFrom(gs, klass) && right->derivesFrom(gs, klass);
}

bool AndType::derivesFrom(const core::GlobalState &gs, core::SymbolRef klass) {
    return left->derivesFrom(gs, klass) || right->derivesFrom(gs, klass);
}

bool AliasType::derivesFrom(const core::GlobalState &gs, core::SymbolRef klass) {
    Error::raise("AliasType.derivesfrom");
}

void AliasType::_sanityCheck(core::Context ctx) {
    ENFORCE(this->symbol.exists());
}

std::shared_ptr<Type> AliasType::instantiate(core::Context ctx, std::vector<SymbolRef> params,
                                             const std::vector<std::shared_ptr<Type>> &targs) {
    Error::raise("should never happen");
}

std::string MetaType::toString(const GlobalState &gs, int tabs) {
    return "MetaType";
}

std::string MetaType::show(const GlobalState &gs) {
    return "<Type: " + wrapped->show(gs) + ">";
}

std::string MetaType::typeName() {
    return "MetaType";
}

void MetaType::_sanityCheck(core::Context ctx) {
    this->wrapped->sanityCheck(ctx);
}

bool MetaType::isFullyDefined() {
    return true; // this is kinda true but kinda false. it's false for subtyping but true for inferencer.
}

bool MetaType::derivesFrom(const core::GlobalState &gs, core::SymbolRef klass) {
    return false;
}

std::shared_ptr<Type> MetaType::instantiate(core::Context ctx, std::vector<SymbolRef> params,
                                            const std::vector<std::shared_ptr<Type>> &targs) {
    Error::raise("should never happen");
}

MetaType::MetaType(shared_ptr<Type> wrapped) : wrapped(wrapped) {}
} // namespace core
} // namespace ruby_typer
