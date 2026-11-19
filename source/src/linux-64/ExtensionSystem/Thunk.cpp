// Linux thunk：用 libffi 按运行时签名调用工具扩展的 .so 导出函数。
// Windows 版生成 x64 机器码；这里保持 Thunk 公共 API 不变，内部换成 ffi_cif/ffi_call。
#include "ExtensionSystem/Thunk.h"

#include <ffi.h>

#include <vector>

namespace
{
	struct FfiState
	{
		ffi_cif cif{};
		std::vector<ffi_type*> argTypes;
		Thunk::Signature signature;
		void* target = nullptr;
	};
}

namespace Thunk
{

Thunk::~Thunk()
{
	delete static_cast<FfiState*>(m_state);
	m_state = nullptr;
}

bool Thunk::build(const Signature& signature, void* target)
{
	delete static_cast<FfiState*>(m_state);
	m_state = nullptr;
	m_code = nullptr;
	m_codeSize = 0;
	m_errorString.clear();

	if (!target) {
		m_errorString = QStringLiteral("thunk target is null");
		return false;
	}
	if (signature.args.size() > 16) {
		m_errorString = QStringLiteral("native caller currently supports at most 16 parameters");
		return false;
	}

	auto* state = new FfiState;
	state->signature = signature;
	state->target = target;
	state->argTypes.reserve(signature.args.size());

	for (ArgType type : signature.args) {
		switch (type) {
		case ArgType::Bool: state->argTypes.push_back(&ffi_type_uint8); break;
		case ArgType::Int: state->argTypes.push_back(&ffi_type_sint32); break;
		case ArgType::Double: state->argTypes.push_back(&ffi_type_double); break;
		case ArgType::String:
		case ArgType::Pointer64: state->argTypes.push_back(&ffi_type_pointer); break;
		case ArgType::Pointer32: state->argTypes.push_back(&ffi_type_uint32); break;
		}
	}

	ffi_type* returnType = &ffi_type_void;
	switch (signature.returnType) {
	case ReturnType::Void: returnType = &ffi_type_void; break;
	case ReturnType::Bool: returnType = &ffi_type_uint8; break;
	case ReturnType::Int: returnType = &ffi_type_sint32; break;
	case ReturnType::Double: returnType = &ffi_type_double; break;
	case ReturnType::String: returnType = &ffi_type_pointer; break;
	}

	if (ffi_prep_cif(&state->cif, FFI_DEFAULT_ABI,
			static_cast<unsigned int>(state->argTypes.size()), returnType,
			state->argTypes.empty() ? nullptr : state->argTypes.data()) != FFI_OK) {
		m_errorString = QStringLiteral("ffi_prep_cif failed");
		delete state;
		return false;
	}

	m_state = state;
	return true;
}

void Thunk::call(const Arg* args, void* result) const
{
	auto* state = static_cast<FfiState*>(m_state);
	if (!state || !state->target)
		return;

	std::vector<void*> values;
	values.reserve(state->signature.args.size());
	for (int i = 0; i < state->signature.args.size(); ++i) {
		switch (state->signature.args.at(i)) {
		case ArgType::Double:
			values.push_back(const_cast<double*>(&args[i].as.d));
			break;
		case ArgType::String:
		case ArgType::Pointer64:
			values.push_back(const_cast<const char**>(&args[i].as.s));
			break;
		case ArgType::Pointer32:
			values.push_back(const_cast<std::uint32_t*>(&args[i].as.u32));
			break;
		case ArgType::Bool:
		case ArgType::Int:
			// ffi_type_uint8/sint32 只读低字节/低 4 字节；Arg 固定 8 字节，小端下取低地址即可。
			values.push_back(const_cast<std::int64_t*>(&args[i].as.i));
			break;
		}
	}

	ffi_call(&state->cif, FFI_FN(state->target), result,
		values.empty() ? nullptr : values.data());
}

QString Thunk::errorString() const
{
	return m_errorString;
}

bool Thunk::emitMachineCode(const Signature&, void*)
{
	m_errorString = QStringLiteral("Linux thunk uses libffi, not machine-code emission");
	return false;
}

} // namespace Thunk
