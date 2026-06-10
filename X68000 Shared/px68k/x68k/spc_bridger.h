#pragma once

#define _TnbLOG_DISABLE

#define _TnbTraceLogger_LogSize 100 * 1024
#include "TnbRs232c.h"
#include "TnbThread.h"
#ifdef _TnbLOG_DISABLE
#define TLOG			Sleep(0); __noop
	const char* GetRegisterName(BYTE addr);
#else
	#include "TnbTraceLogger.h"
	inline const char* GetRegisterName(BYTE addr)
	{
		const char * p = "--";
		switch ( addr )
		{
		case 0x01:case 0x81: p = "BDID"; break;
		case 0x03:case 0x83: p = "SCTL"; break;
		case 0x05:case 0x85: p = "SCMD"; break;
		case 0x09:case 0x89: p = "INTS"; break;
		case 0x0B: p = "PSNS"; break;
		case 0x8B: p = "SDGC"; break;
		case 0x0D:case 0x8D: p = "SSTS"; break;
		case 0x0F:case 0x8F: p = "SERR"; break;
		case 0x11:case 0x91: p = "PCTL"; break;
		case 0x13:case 0x93: p = "MBC";	break;
		case 0x15:case 0x95: p = "DREG"; break;
		case 0x17:case 0x97: p = "TEMP"; break;
		case 0x19:case 0x99: p = "TCH"; break;
		case 0x1B:case 0x9B: p = "TCM"; break;
		case 0x1D:case 0x9D: p = "TCL"; break;
		case 0x41: p = "DREG-EX"; break;		//特殊CMD
		case 0x1F:case 0x9F: p = "IOCS"; break;	//独自IO
		}
		return p;
	}
#endif

#include "spc_bridger_vector.h"

/**
 * SPC Bridger クラス
 */
class SPC_BRIDGER : private CThread::IRunner
{
public:

	/// コンストラクタ
	SPC_BRIDGER(void) 
		: 	m_sendingIocsPos(0), m_exBufSize(0), m_intsCount(0), m_pInts(NULL), m_tc(0), m_isInit(false)
	{
	}

	/**
	 * 初期化.
	 *	@param iInts 割り込みメソッド
	 *	@retval true 成功
	 *	@retval false 失敗
	 */
	bool Init(IScsiIntsVector* iInts)
	{
		Stop();
		CWordVector vp = CRs232c::EnumExistPortsByUsb(0x04d8, 0xE6B2);
		loop ( i , vp )
		{
			if ( m_Start(m_rs232c, vp[i]) && ExecCmdRs(m_rs232c, 0x00) == 's' )
			{
				loop ( j , vp )
				{
					if ( m_Start(m_rs232c2, vp[j]) && ExecCmdRs(m_rs232c2, 0x00) == 'S' )
					{
						m_sendDataLen = 0;
						m_tickQueue = ::GetTickCount();
						m_StartThread(iInts);
						m_exBufSize = 0;
						m_intsCount = 0;
						m_tc = 0;
						m_isInit = true;
						m_lastSctl = -1;
//						MessageBox(NULL, "    SPC bridge mode    ", "XM6", MB_OK);
						SendCmd(0x80, 0xFE);
						return true;
					}
					m_rs232c2.Close();
				}
			}
			m_rs232c2.Close();
			m_rs232c.Close();
		}
		Stop();
		return false;
	}

	/**
	 * 停止
	 */
	void Stop(void)
	{
		m_Stop();
		m_isInit = false;
	}

	/** 生存確認 
	 *	@retval true 生存 or 居た
	 *	@retval false いない
	 */
	bool IsAlive(void) const
	{
		return m_isInit;
	}

	/** 
	 * コマンド実行
	 *	@param cmd コマンド
	 *	@param d ダイアログあり？
	 *	@reval 0xFF エラー
	 *	@reval 値 コマンドに対するレスポンス
	 */
	BYTE ExecCmd(BYTE cmd, bool d = true)
	{
		if ( m_isInit && !m_rs232c2.IsOpened() ) 
		{
			return 0x00;
		}
		if ( cmd == 0x15/*DREG*/ && m_tc >= 512 && (m_lastRegValue[0x11/*PCTL*/] & 7) == 0x01)
		{
			//DRAGでデータインフェーズ
			m_intsCount = 0;
			return m_ExecCmdEx(0x41/*DREG-EX*/);
		}
		if ( cmd == 0x1F/*IOCS*/ )
		{
			// 隠しIOCSポート
			m_intsCount = 0;
			return m_ExecCmdEx(cmd);
		}
		if ( cmd == 0x03/*SCTL*/ && m_lastSctl >= 0) 
		{
			return (BYTE)m_lastSctl;
		}
		//
		TLOG("★Exec(0x%02X[%s])", cmd, GetRegisterName(cmd));
		int r = 0;
		if ( cmd == 0x09/*INTS*/ && m_exBufSize > 0 )
		{
			TLOG("       cache");
			if ( ++m_intsCount > 8 )
			{
				TLOG("       cache end");
				m_intsCount = 0;
				m_exBufSize = 0;
				r = 0x10;
			}
		}
		else if ( cmd == 0x0D/*SSTS*/ && m_exBufSize > 0 )
		{
			TLOG("       cache");
			r = 0xB2;
		}
		else
		{
			if ( cmd == 0x09/*INTS*/ )
			{
				m_intsCount = 0;
			}
			r = m_Exec(m_rs232c, cmd, 5000000);  //
		}
		if ( r >= 0 )
		{
			TLOG("       - Res = 0x%02X", r);
			return static_cast<BYTE>(r);
		}
		TLOG("       - Timeout");
		m_Stop();
		if (d) 
		{
			MessageBox(NULL, "    end SPC bridge mode    ", "XM6", MB_OK);
		}
		return 0xFF;
	}

	/** 
	 * コマンド送信
	 *	@param cmd コマンド
	 *	@param prm パラメータ
	 */
	void SendCmd(BYTE cmd, BYTE prm)
	{
		if ( m_isInit && !m_rs232c2.IsOpened() ) 
		{
			return;
		}
		if (cmd == 0x03/*SCTL*/ && m_lastSctl == (int)(prm & 0xFF))
		{
			return;
		}
		TLOG("★Send(0x%02X[%s],0x%02X)", cmd, GetRegisterName((BYTE)(cmd | 0x80)), prm);
		if ( m_sendingIocsPos > 0 ) 
		{
			TLOG("  --iocsmode");
			m_Send(m_rs232c, prm);
			if ( m_sendingIocsPos < 16 ) 
			{
				m_iocsData[m_sendingIocsPos] = prm;
			}
			m_sendingIocsPos++;
			if (m_sendingIocsPos >= 2)
			{
				int len = (m_iocsData[1] * 0x100) | m_iocsData[0];
				if (len == m_sendingIocsPos - 2) {
					m_sendingIocsPos = 0;
				}
			}
			return;
		}
		if ( cmd == 0x1F )
		{
			m_sendingIocsPos = 0;
			m_iocsData[m_sendingIocsPos++] = prm;
		}
		m_Send(m_rs232c, (BYTE)(cmd | 0x80));
		m_Send(m_rs232c, prm);
		m_lastRegValue[cmd] = prm;
		if ( cmd == 0x19 || cmd == 0x1B || cmd == 0x1D )
		{
			m_tc = (m_lastRegValue[0x19/*tch*/] << 16) | (m_lastRegValue[0x1b/*tcm*/] << 8 ) | m_lastRegValue[0x1D/*tcl*/];
		}
		if ( cmd == 0x05/*SCMD*/ && (prm & 0xE0) == 0x20 && m_exBufSize > 0 )
		{
			// セレクション実行
			m_exBufSize = 0;
		}
		if (cmd == 0x03/*SCTL*/) 
		{
			m_lastSctl = prm & 0xFF;
		}
	}

	/** 
	 * コマンド実行（pipe2）
	 *	@param cmd コマンド
	 *	@reval マイナス エラー
	 *	@reval 値 コマンドに対するレスポンス
	 */
	int ExecCmdRs(CRs232c& rs, BYTE cmd)
	{
		TLOG("★ExecRs(0x%02X[%s])", cmd, GetRegisterName(cmd));
		int r = m_Exec(rs, cmd, 200000); //一秒
		if ( r >= 0 )
		{
			TLOG("       - Res = 0x%02X", r);
			return r;
		}
		TLOG("       - Timeout");
		return -1;
	}

private:

	/**
	 * 停止
	 */
	void m_Stop(void)
	{
		m_thread.Stop();
		m_rs232c.Close();
		m_rs232c2.Close();
	}

	/**
	 * 開始
	 *	@param rs RS232Cオブジェクト
	 *	@param port COMポート番号
	 *	@retval true 成功
	 *	@retval false 失敗
	 */
	bool m_Start(CRs232c& rs, int port)
	{
		rs.Close();
		rs.SetParameter(port, 12000000 * 40, 8, CRs232c::Parity_Non, CRs232c::StopBits_1);
		return rs.Open();
	}

	/** 
	 * スレッド開始
	 *	@param i 割り込みメソッド
	 */
	void m_StartThread(IScsiIntsVector* i)
	{
		m_pInts = i;
		m_thread.SetRunner(this);
		m_thread.Start();
	}

	/** 
	 * スレッド本体
	 *	@return 0
	 */
	virtual DWORD Run(void)
	{
		BYTE b;
		m_tickQueue = ::GetTickCount();
		while ( IsRunnable() && m_rs232c2.IsOpened() )
		{
			if ( m_rs232c2.Receive(1, &b) == 1 )
			{
				TLOG("★Init(0x%02X)", b);
				if ( m_pInts != NULL && b == 'I' )
				{
					m_pInts->OnIntCheck();
				}
			}
			if (::GetTickCount() - m_tickQueue > 500) 
			{
				m_SendQueue(m_rs232c);
				m_tickQueue = ::GetTickCount();
			}
			::Sleep(0);
		}
		return 0;
	}

	/**
	 * 送信実行.
	 *	@param rs RS232Cオブジェクト
	 *  @param b データ
	 */
	void m_Send(CRs232c& rs, BYTE b)
	{
		EXCLUSIVE(&m_syncQueue);
		m_sendData[m_sendDataLen++] = b;
		if (m_sendDataLen > 1024 * 4) 
		{
			int r = rs.Send(m_sendDataLen, m_sendData);
			(void)r;
			m_sendDataLen = 0;
		}
		m_tickQueue = ::GetTickCount();
	}

	/**
	 * 送信実行(キューのものを送りだす）
	 *	@param rs RS232Cオブジェクト
	 */
	void m_SendQueue(CRs232c& rs)
	{
		EXCLUSIVE(&m_syncQueue);
		if (m_sendDataLen > 0) 
		{
			int r = rs.Send(m_sendDataLen, m_sendData);
			(void)r;
			m_sendDataLen = 0;
		}
	}

	/**
	 * 実行
	 *	@param rs RS232Cオブジェクト
	 *	@param cmd コマンド
	 *	@retval マイナス エラー
	 *	@retval 値 受信値
	 */
	int m_Exec(CRs232c& rs, BYTE cmd, DWORD count)
	{
		m_sendingIocsPos = 0;
		m_exBufSize = 0;
		m_exBufPos = 0;
		rs.Purge();
		m_SendQueue(rs);
		if ( rs.Send(1, &cmd) == 1 )
		{
			BYTE buf;
			loop ( i, count )
			{
				if ( rs.Receive(1, &buf) == 1 )
				{
					return buf;
				}
				::Sleep(0);
			}
			return -1;
		}
		TLOG("       - Send Error(%08X)", ::GetLastError());
		return -2;
	}

	/**
	 * 拡張コマンド実行(0x4x専用)
	 *	@param cmd コマンド
	 *	@retval 0xFF エラー
	 *	@retval 値 受信値（キャッシュからの場合もあり）
	 */
	BYTE m_ExecCmdEx(BYTE cmd)
	{
		TLOG("★ExecEx(0x%02X[%s])", cmd, GetRegisterName(cmd));
		if ( m_exBufSize == 0 )
		{
			int r = m_ExecEx(m_rs232c, cmd);
			TLOG("    recv - (%d)", r);
			if ( r < 0 )
			{
				TLOG("       - Timeout");
				m_Stop();
				MessageBox(NULL, "    end SPC bridge mode    ", "XM6", MB_OK);
				return 0xFF;
			}
		}
		else
		{
			TLOG("       cache");
		}
		if ( m_exBufSize == 0 ) 
		{
			return 0x00;
		}
		BYTE b = m_exBuf[m_exBufPos++];
		if ( m_exBufPos >= m_exBufSize )
		{
			m_exBufSize = 0;
			m_exBufPos = 0;
			TLOG("     - Cache Empty");
		}
		TLOG("       - Res = 0x%02X", b);
		return b;
	}

	/**
	 * 拡張実行（0x4x専用）
	 *	@param rs RS232Cオブジェクト
	 *	@param cmd コマンド
	 *	@retval マイナス エラー
	 *	@retval 値 受信数
	 */
	int m_ExecEx(CRs232c& rs, BYTE cmd)
	{
		m_SendQueue(rs);
		rs.Purge();
		m_exBufSize = 0;
		m_exBufPos = 0;
		size_t s = rs.Send(1, &cmd);
		if ( s == 1 )
		{
			WORD len = 0;
			loop ( i, 100000000 )
			{
				::Sleep(0);
				if ( rs.Receive(2, &len) == 2 )
				{
					while ( true )
					{
						size_t r = rs.Receive(len, &m_exBuf[m_exBufSize]);
						if ( r == INVALID_SIZE )
						{
							TLOG("       - recv error");
							return -1;
						}
						m_exBufSize += r;
						len = static_cast<WORD>(len - r);
						if ( len == 0 )
						{
							return m_exBufSize;
						}
					}
				}
			}
			TLOG("       - Time out");
			return -1;
		}
		TLOG("       - Send Error(%08X)(%08X)", s, ::GetLastError());
		return -2;
	}

	bool			m_isInit;			///< 初期化した？
	CThread			m_thread;			///< 受信スレッド
	IScsiIntsVector* m_pInts;			///< 割り込みベクター
	CRs232c			m_rs232c;			///< RS232c Port 1
	CRs232c			m_rs232c2;			///< RS232c Port 2
	BYTE			m_sendData[1024 * 4 + 16]; ///< 送信データ
	DWORD			m_sendDataLen;		///< 送信データ長さ
	int				m_sendingIocsPos;	///< 送信データ長さ
	BYTE			m_iocsData[16];		///< 送信バッファ
	BYTE			m_exBuf[0x10000];	///< 受信バッファ
	int				m_exBufSize;		///< 受信バッファーサイズ
	int				m_exBufPos;			///< 受信参照位置
	BYTE			m_lastRegValue[32];	///< 各コマンドの最終設定値
	DWORD			m_tc;				///< TC	レジスタの最終設定値
	DWORD			m_intsCount;		///< INTSレジスタ連続読込カウンタ
	int				m_lastSctl;			///< SCTLレジスタの最終書き込み値
	CSyncSection	m_syncQueue;		///< キュー排他
	volatile DWORD	m_tickQueue;		///< キュー送信用TICK
};

