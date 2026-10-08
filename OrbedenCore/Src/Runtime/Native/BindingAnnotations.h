#pragma once

//不向托管 API 公开内部成员；不影响反射与序列化。
#define ORBEDEN_BIND_IGNORE
//字段读写必须经显式 getter/setter；使用 None 表示只读或只写。
#define ORBEDEN_BIND_ACCESSORS(GETTER, SETTER)
//把方法的指针参数与长度参数映射为一个托管 Span。
#define ORBEDEN_BIND_BUFFER(DATA, COUNT)

//字段写入后显式执行通知；同时供反射和 Binding 使用。
#define ORBEDEN_BIND_CHANGED(CALLBACK)
//组件只能在一个 Ens 上存在一个实例。
#define ORBEDEN_COMPONENT_UNIQUE

//跨语言合同的枚举：即使没有被绑定成员引用，也要导出到托管侧。
//用于 RetainedGui ABI 这类按值直接使用枚举、没有绑定成员引用点的场合。
#define ORBEDEN_BIND_EXPORT