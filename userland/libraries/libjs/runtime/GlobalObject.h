/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

// includes
#include <libjs/heap/Heap.h>
#include <libjs/runtime/Environment.h>
#include <libjs/runtime/VM.h>

namespace JS {

class GlobalObject : public Object {
    JS_OBJECT(GlobalObject, Object);

public:
    explicit GlobalObject();
    virtual void initialize_global_object();

    virtual ~GlobalObject() override;

    GlobalEnvironment& environment() { return *m_environment; }

    Console& console() { return *m_console; }

    Shape* empty_object_shape() { return m_empty_object_shape; }

    Shape* new_object_shape() { return m_new_object_shape; }
    Shape* new_ordinary_function_prototype_object_shape() { return m_new_ordinary_function_prototype_object_shape; }

    ProxyConstructor* proxy_constructor() { return m_proxy_constructor; }

    GeneratorObjectPrototype* generator_object_prototype() { return m_generator_object_prototype; }

    FunctionObject* array_prototype_values_function() const { return m_array_prototype_values_function; }
    FunctionObject* eval_function() const { return m_eval_function; }
    FunctionObject* temporal_time_zone_prototype_get_offset_nanoseconds_for_function() const { return m_temporal_time_zone_prototype_get_offset_nanoseconds_for_function; }
    FunctionObject* throw_type_error_function() const { return m_throw_type_error_function; }

#define __JS_ENUMERATE(ClassName, snake_name, PrototypeName, ConstructorName, ArrayType) \
    ConstructorName* snake_name##_constructor() { return m_##snake_name##_constructor; } \
    Object* snake_name##_prototype() { return m_##snake_name##_prototype; }
    JS_ENUMERATE_BUILTIN_TYPES
#undef __JS_ENUMERATE

#define __JS_ENUMERATE(ClassName, snake_name, PrototypeName, ConstructorName)                              \
    Intl::ConstructorName* intl_##snake_name##_constructor() { return m_intl_##snake_name##_constructor; } \
    Object* intl_##snake_name##_prototype() { return m_intl_##snake_name##_prototype; }
    JS_ENUMERATE_INTL_OBJECTS
#undef __JS_ENUMERATE

#define __JS_ENUMERATE(ClassName, snake_name, PrototypeName, ConstructorName)                                          \
    Temporal::ConstructorName* temporal_##snake_name##_constructor() { return m_temporal_##snake_name##_constructor; } \
    Object* temporal_##snake_name##_prototype() { return m_temporal_##snake_name##_prototype; }
    JS_ENUMERATE_TEMPORAL_OBJECTS
#undef __JS_ENUMERATE

#define __JS_ENUMERATE(ClassName, snake_name) \
    Object* snake_name##_prototype() { return m_##snake_name##_prototype; }
    JS_ENUMERATE_ITERATOR_PROTOTYPES
#undef __JS_ENUMERATE

protected:
    virtual void visit_edges(Visitor&) override;

    template<typename ConstructorType>
    void initialize_constructor(PropertyName const&, ConstructorType*&, Object* prototype);
    template<typename ConstructorType>
    void add_constructor(PropertyName const&, ConstructorType*&, Object* prototype);

private:
    virtual bool is_global_object() const final { return true; }

    JS_DECLARE_NATIVE_FUNCTION(gc);
    JS_DECLARE_NATIVE_FUNCTION(is_nan);
    JS_DECLARE_NATIVE_FUNCTION(is_finite);
    JS_DECLARE_NATIVE_FUNCTION(parse_float);
    JS_DECLARE_NATIVE_FUNCTION(parse_int);
    JS_DECLARE_NATIVE_FUNCTION(eval);
    JS_DECLARE_NATIVE_FUNCTION(encode_uri);
    JS_DECLARE_NATIVE_FUNCTION(decode_uri);
    JS_DECLARE_NATIVE_FUNCTION(encode_uri_component);
    JS_DECLARE_NATIVE_FUNCTION(decode_uri_component);
    JS_DECLARE_NATIVE_FUNCTION(escape);
    JS_DECLARE_NATIVE_FUNCTION(unescape);

    NonnullOwnPtr<Console> m_console;

    Shape* m_empty_object_shape { nullptr };
    Shape* m_new_object_shape { nullptr };
    Shape* m_new_ordinary_function_prototype_object_shape { nullptr };

    ProxyConstructor* m_proxy_constructor { nullptr };

    GeneratorObjectPrototype* m_generator_object_prototype { nullptr };

    GlobalEnvironment* m_environment { nullptr };

    FunctionObject* m_array_prototype_values_function { nullptr };
    FunctionObject* m_eval_function { nullptr };
    FunctionObject* m_temporal_time_zone_prototype_get_offset_nanoseconds_for_function { nullptr };
    FunctionObject* m_throw_type_error_function { nullptr };

#define __JS_ENUMERATE(ClassName, snake_name, PrototypeName, ConstructorName, ArrayType) \
    ConstructorName* m_##snake_name##_constructor { nullptr };                           \
    Object* m_##snake_name##_prototype { nullptr };
    JS_ENUMERATE_BUILTIN_TYPES
#undef __JS_ENUMERATE

#define __JS_ENUMERATE(ClassName, snake_name, PrototypeName, ConstructorName) \
    Intl::ConstructorName* m_intl_##snake_name##_constructor { nullptr };     \
    Object* m_intl_##snake_name##_prototype { nullptr };
    JS_ENUMERATE_INTL_OBJECTS
#undef __JS_ENUMERATE

#define __JS_ENUMERATE(ClassName, snake_name, PrototypeName, ConstructorName)     \
    Temporal::ConstructorName* m_temporal_##snake_name##_constructor { nullptr }; \
    Object* m_temporal_##snake_name##_prototype { nullptr };
    JS_ENUMERATE_TEMPORAL_OBJECTS
#undef __JS_ENUMERATE

#define __JS_ENUMERATE(ClassName, snake_name) \
    Object* m_##snake_name##_prototype { nullptr };
    JS_ENUMERATE_ITERATOR_PROTOTYPES
#undef __JS_ENUMERATE
};

template<typename ConstructorType>
inline void GlobalObject::initialize_constructor(PropertyName const& property_name, ConstructorType*& constructor, Object* prototype)
{
    auto& vm = this->vm();
    constructor = heap().allocate<ConstructorType>(*this, *this);
    constructor->define_direct_property(vm.names.name, js_string(heap(), property_name.as_string()), Attribute::Configurable);
    if (vm.exception())
        return;
    if (prototype) {
        prototype->define_direct_property(vm.names.constructor, constructor, Attribute::Writable | Attribute::Configurable);
        if (vm.exception())
            return;
    }
}

template<typename ConstructorType>
inline void GlobalObject::add_constructor(PropertyName const& property_name, ConstructorType*& constructor, Object* prototype)
{
    if (!constructor)
        initialize_constructor(property_name, constructor, prototype);
    define_direct_property(property_name, constructor, Attribute::Writable | Attribute::Configurable);
}

inline GlobalObject* Shape::global_object() const
{
    return static_cast<GlobalObject*>(m_global_object);
}

template<>
inline bool Object::fast_is<GlobalObject>() const { return is_global_object(); }

template<typename... Args>
[[nodiscard]] ALWAYS_INLINE Value Value::invoke(GlobalObject& global_object, PropertyName const& property_name, Args... args)
{
    if constexpr (sizeof...(Args) > 0) {
        MarkedValueList arglist { global_object.vm().heap() };
        (..., arglist.append(move(args)));
        return invoke_internal(global_object, property_name, move(arglist));
    }

    return invoke_internal(global_object, property_name, Optional<MarkedValueList> {});
}

}