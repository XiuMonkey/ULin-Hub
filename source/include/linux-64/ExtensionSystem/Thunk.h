#pragma once

// thunk 生成器：按参数/返回值描述把统一参数数组转换成目标函数的真实调用约定，由 DllCaller 的
// native 调用风格（invokeNativeFunction）使用。Linux 版内部走 libffi（Windows 版生成 x64 机器码）。

#include <QVector>
#include <QString>
#include <cstddef>
#include <cstdint>

namespace Thunk
{
	enum class ArgType
	{
		Bool,
		Int,
		Double,
		String,
		Pointer32,
		Pointer64
	};

	enum class ReturnType
	{
		Void,
		Bool,
		Int,
		Double,
		String
	};

	struct Signature
	{
		ReturnType returnType = ReturnType::Void;
		QVector<ArgType> args;
	};

	// 统一 thunk 参数：每个参数固定占 8 字节；Bool/Int/String 使用 int64/pointer，Double 使用 double 的位模式。
	struct Arg
	{
		union
		{
			std::int64_t i = 0;
			double d;
			const char* s;
			std::uint32_t u32;
		} as;
	};

	// 生成的 thunk 入口统一使用该签名：args 指向参数数组，result 指向返回值缓冲区。
	using ThunkFn = void (*)(const Arg* args, void* result);

	class Thunk
	{
	public:
		Thunk() = default;
		~Thunk();

		Thunk(const Thunk&) = delete;
		Thunk& operator=(const Thunk&) = delete;

		// 根据签名和目标函数地址生成 thunk：成功返回 true，失败可通过 errorString 获取原因。
		bool build(const Signature& signature, void* target);

		// 调用 thunk：args 是参数数组，result 指向返回值缓冲区（返回 void 时给个哑元即可）。
		void call(const Arg* args, void* result) const;

		QString errorString() const;

		// 仅用于测试/诊断：机器码的首字节与长度。Linux 版走 libffi、不生成机器码，故恒为 nullptr / 0；
		// Windows 版用例靠它校验栈帧调整用的是 imm32 形式（参数一多就写穿调用者栈帧的那个边界）。
		const std::uint8_t* codeBytes() const
		{
			return static_cast<const std::uint8_t*>(m_code);
		}

		std::size_t codeSize() const
		{
			return m_codeSize;
		}

	private:
		// 仅 Windows 版实现；Linux 版恒失败（内部走 libffi，不生成机器码）。
		bool emitMachineCode(const Signature& signature, void* target);

		void* m_code = nullptr; // Windows 版生成的机器码；Linux 版恒为空
		std::size_t m_codeSize = 0;
		// Linux 版的 libffi 状态（FfiState*）；Windows 版不用它，保留字段只为两平台头文件形状一致。
		void* m_state = nullptr;
		QString m_errorString;
	};
} // namespace Thunk
