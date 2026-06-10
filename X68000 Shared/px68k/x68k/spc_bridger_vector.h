#pragma once

/**
 * SCSI割り込みベクターインターフェース
 */
struct IScsiIntsVector
{
	/// 割り込み
	virtual void FASTCALL OnIntCheck() = 0;			// 割り込みチェック
};