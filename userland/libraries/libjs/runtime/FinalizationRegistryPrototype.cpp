/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// includes
#include <base/TypeCasts.h>
#include <libjs/runtime/FinalizationRegistryPrototype.h>

namespace JS {

FinalizationRegistryPrototype::FinalizationRegistryPrototype(GlobalObject& global_object)
    : Object(*global_object.object_prototype())
{
}

void FinalizationRegistryPrototype::initialize(GlobalObject& global_object)
{
    auto& vm = this->vm();
    Object::initialize(global_object);
    u8 attr = Attribute::Writable | Attribute::Configurable;

    define_native_function(vm.names.cleanupSome, cleanup_some, 0, attr);
    define_native_function(vm.names.register_, register_, 2, attr);
    define_native_function(vm.names.unregister, unregister, 1, attr);

    define_direct_property(*vm.well_known_symbol_to_string_tag(), js_string(global_object.heap(), vm.names.FinalizationRegistry.as_string()), Attribute::Configurable);
}

FinalizationRegistryPrototype::~FinalizationRegistryPrototype()
{
}

FinalizationRegistry* FinalizationRegistryPrototype::typed_this(VM& vm, GlobalObject& global_object)
{
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return nullptr;
    if (!is<FinalizationRegistry>(this_object)) {
        vm.throw_exception<TypeError>(global_object, ErrorType::NotA, "FinalizationRegistry");
        return nullptr;
    }
    return static_cast<FinalizationRegistry*>(this_object);
}

JS_DEFINE_NATIVE_FUNCTION(FinalizationRegistryPrototype::cleanup_some)
{
    auto* finalization_registry = typed_this(vm, global_object);
    if (!finalization_registry)
        return {};

    auto callback = vm.argument(0);
    if (vm.argument_count() > 0 && !callback.is_function()) {
        vm.throw_exception<TypeError>(global_object, ErrorType::NotAFunction, callback.to_string_without_side_effects());
        return {};
    }

    finalization_registry->cleanup(callback.is_undefined() ? nullptr : &callback.as_function());

    return js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(FinalizationRegistryPrototype::register_)
{
    auto* finalization_registry = typed_this(vm, global_object);
    if (!finalization_registry)
        return {};

    auto target = vm.argument(0);
    if (!target.is_object()) {
        vm.throw_exception<TypeError>(global_object, ErrorType::NotAnObject, target.to_string_without_side_effects());
        return {};
    }

    auto held_value = vm.argument(1);
    if (same_value(target, held_value)) {
        vm.throw_exception<TypeError>(global_object, ErrorType::FinalizationRegistrySameTargetAndValue);
        return {};
    }

    auto unregister_token = vm.argument(2);
    if (!unregister_token.is_object() && !unregister_token.is_undefined()) {
        vm.throw_exception<TypeError>(global_object, ErrorType::NotAnObject, unregister_token.to_string_without_side_effects());
        return {};
    }

    finalization_registry->add_finalization_record(target.as_cell(), held_value, unregister_token.is_undefined() ? nullptr : &unregister_token.as_object());

    return js_undefined();
}

JS_DEFINE_NATIVE_FUNCTION(FinalizationRegistryPrototype::unregister)
{
    auto* finalization_registry = typed_this(vm, global_object);
    if (!finalization_registry)
        return {};

    auto unregister_token = vm.argument(0);
    if (!unregister_token.is_object()) {
        vm.throw_exception<TypeError>(global_object, ErrorType::NotAnObject, unregister_token.to_string_without_side_effects());
        return {};
    }

    return Value(finalization_registry->remove_by_token(unregister_token.as_object()));
}

}