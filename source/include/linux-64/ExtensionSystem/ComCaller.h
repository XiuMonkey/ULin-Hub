#pragma once

// InterfaceType = "com" 的独立执行器：参数以 JSON 形式传输，执行 COM 自动化（IDispatch），与 DllCaller 的 json/native 风格并列。
// 组件（ProgId）在 regulation.json5 的 "Com" 段白名单式声明，运行时 args 只能控制 member / kind(method|get|put) / path / params / value。
// 返回：成功写入 { "value": <JSON 标量> }（结果为对象时附 "object": true，仅摘要）；失败返回 false、error 给出可读信息。
// Linux 没有 COM/IDispatch：本平台的 invoke 恒失败并写明原因，接口形状与 Windows 版保持一致。

#include <QJsonObject>
#include <QString>

namespace comcall
{
	// 执行一次 COM 调用。成功返回 true 并把结果写入 result；失败返回 false 且 error 携带错误信息。
	bool invoke(const QJsonObject& comConfig, const QJsonObject& args, QJsonObject& result, QString& error);
} // namespace comcall
