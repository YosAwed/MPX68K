#pragma once


#define _TnbLOG_DISABLE

#define _TnbTraceLogger_LogSize 100 * 1024
#include "TnbRs232c.h"
#include "TnbThread.h"
#ifdef _TnbLOG_DISABLE
#define TLOG			__noop
#else
	#include "TnbTraceLogger.h"
#endif


/**
 * 8255クラス
 */
class C8255 : CThread::IRunner
{
public:

	/// コンストラクタ
	C8255(void) : m_isRecvEndMark(false), m_controlWordNext(-1), m_isCommandOldMode(false)
	{
	}

	/**
	 * 初期化（アクティブ化）
	 *	@note 既定のVID,PIDのCOMポートに対し初期化コマンドを投げて応答を確認する。
	 *	@param isNotifyMode 0;コマンドモードで初期化。1;通知モードで初期化
	 *	@param withDialog trueならダイアログも出します。
	 *	@retval 成功。
	 *	@retval 失敗。
	 */
	bool Init(int isNotifyMode, bool withDialog = true)
	{
		Stop();
		m_isNotifyMode = false;
		m_controlWord = 0x92;
		CWordVector vp = CRs232c::EnumExistPortsByUsb(0x04d8, 0xE6B3);
		loop ( i , vp )
		{
			if ( StartCom(vp[i]) )
			{
				m_isNotifyMode = true;
				BYTE d = static_cast<BYTE>(isNotifyMode ? 0x31 : 0x37);	
				m_isCommandOldMode = false;
				if ( d == 0x37 )
				{
					BYTE b1[7];
					BYTE b2 = 'v';
					m_rs232c.Send(1, &b2);
					::Sleep(100);
					size_t l = m_rs232c.Receive(7, b1);
					m_rs232c.Purge();
					if ( l >= 2 && b1[1] < '2' )
					{
						d = 0x31;
						m_isCommandOldMode = true;
					}
				}
				StartThread();
				TLOG("★Send(0x%02X)", d);
				size_t l = m_rs232c.Send(1, &d);
				if ( l == 1 )
				{
					loop ( i, 5000000 ) 
					{
						if ( IsRecvEndMark() )
						{
							TLOG("       - Recv(0x%02X)", m_recvData);
							if ( m_recvData == 0x31 )
							{
								m_isNotifyMode = true;
								if ( withDialog )
								{
									MessageBox(NULL, "    8255 emulation notify mode    ", "XM6", MB_OK);
								}
								return true;
							}
							else if ( m_recvData == 0x37 )
							{
								m_thread.Stop();
								m_isNotifyMode = false;
								if ( withDialog )
								{
									MessageBox(NULL, "    8255 emulation command mode    ", "XM6", MB_OK);
								}
								return true;
							}
							else if ( m_recvData == 0x31 )
							{
								m_thread.Stop();
								m_isNotifyMode = false;
								if ( withDialog )
								{
									MessageBox(NULL, "    8255 emulation command(old) mode    ", "XM6", MB_OK);
								}
								return true;
							}
							break;
						}
						::Sleep(0);
					}
				}
				Stop();
			}
		}
		return false;
	}

	/// 停止
	void Stop(void)
	{
		m_rs232c.Close();
		m_thread.Stop();
	}

	/**
	 * アクティブ確認？
	 *	@retval true アクティブ
	 *	@retval false 停止中
	 */
	bool IsAlive(void) const
	{
		return m_rs232c.IsOpened();
	}

	/**
	 * コマンド実行.
	 * @note 通知モード際は内部変数値を返すだけ
	 *	@param cmd コマンド
	 *	@reval -1 失敗。非アクティブになります。
	 *	@reval 0以上 成功。取得した値（BYTE）が返ります。
	 */
	int ExecCmd(BYTE cmd)
	{
		TLOG("★Send(0x%02X)", cmd);
		if ( m_isNotifyMode )
		{
			m_CheckCtrlword();
			switch ( cmd )
			{
			case 0x3A:
				TLOG("     Recv(0x%02X)", m_portA);
				return m_portA;
			case 0x3B:
				TLOG("     Recv(0x%02X)", m_portB);
				return m_portB;
			case 0x3C:
				TLOG("     Recv(0x%02X)", m_portC);
				return m_portC;
			default:
				break;
			}
			return 0;
		}
		m_isRecvEndMark = false;
		if ( cmd == 0x3A && (m_controlWord & 0x10) == 0 )
		{
			TLOG("     Recv(0x%02X)", m_portA);
			return m_portA;
		}
		if ( cmd == 0x3B && (m_controlWord & 0x02) == 0 )
		{
			TLOG("     Recv(0x%02X)", m_portB);
			return m_portB;
		}
		if ( cmd == 0x3C && (m_controlWord & 0x08) == 0 )
		{
			TLOG("     Recv(0x%02X)", m_portC);
			return m_portC;
		}
		//
		BYTE b1 = 0;
		if ( cmd == 0x3A && m_datasPortA.GetSize() > 0 )
		{
			if ( m_datasPortA.GetSize() == m_datasPosition )
			{
				m_datasPosition = 0;
				m_datasPortA.Free();
			}
			else
			{
				b1 = m_datasPortA[m_datasPosition++];
				TLOG("     Recv*(0x%02X) - %d", b1, m_datasPosition);
				m_lastCmd = cmd;
				return b1;
			}
		}
		TLOG("  ->send(0x%02X)", cmd);
		//::Sleep(0);
		size_t r = m_rs232c.Send(1, &cmd);
		if ( r == 1 )
		{
			loop ( i, 500000 )
			{
				size_t l = m_rs232c.Receive(1, &b1);
				::Sleep(0);
				if ( i == INVALID_SIZE )
				{
					break;
				}
				else if ( l == 1 )
				{
					if ( !m_isCommandOldMode && cmd == 0x3A && (b1 & 0x80) == 0 && b1 > 0 )
					{
						TLOG("     Recv Length = %d", b1);
						m_datasPortA.Resize(b1);
						int k = 0;
						while ( k < b1 )
						{
							l = m_rs232c.Receive(b1 - k, &m_datasPortA[k]);
							TLOG("        -recv l = %d", l);
							k += l;
							::Sleep(0);
						}
						m_datasPosition = 1;
						b1 = m_datasPortA[0];
						TLOG("     Recv*(0x%02X)", b1);
						m_lastCmd = cmd;
						return b1;
					}
					TLOG("     Recv(0x%02X)", b1);				
					m_lastCmd = cmd;
					return b1;
				}
				::Sleep(0);
			}
			TLOG("       - Timeout3");
		}
		TLOG("       - Timeout2");
		Stop();
		MessageBox(NULL, "    end 8255 emulation mode 1   ", "XM6", MB_OK);
		return -1;
	}

	/**
	 * 送信実行.
	 * @note 通知モード際は応答も待ちます。
	 *	@param cmd コマンド
	 *	@param data データ
	 *	@reval false 失敗。非アクティブになります。
	 *	@reval true 成功。
	 */
	bool Send(BYTE cmd, BYTE data)
	{
		TLOG("★Send(0x%02X,0x%02X)", cmd, data);
		int b = -1;
		switch ( cmd )
		{
		case 0x4A:
			b = (data & _BIN(01101111)) | _BIN(10000000);
			m_portA = data;
			break;
		case 0x4B:
			b = (data & _BIN(01101111)) | _BIN(10010000);
			m_portB = data;
			break;
		case 0x4C:
			b = data >> 4;
			m_portC = data;
			break;
		case 0x4D:
			if ( (data & 0x80) != 0 )
			{
				if ( m_isNotifyMode )
				{
					if ( m_controlWordNext >= 0 )
					{
						m_controlWordNext = data;
						if ( m_controlWord == data )
						{
							m_controlWordNext = -1;
						}
					}
					else
					{
						m_controlWordNext = data;
					}
				}
				else
				{
					b = ((data & _BIN(01111000)) >> 1) | (data & _BIN(00000011)) | _BIN(01000000);
					m_controlWord = data;
				}
			}
			else
			{
				static int ss = -1;
				if ( data != ss ) 
				{
					ss = data;
					b = (data & 0x0F) | _BIN(00010000);
				}
			}
			break;
		default:
			break;
		}
		if ( b < 0 )
		{
			m_lastCmd = cmd;
			return true;
		}
		BYTE b1 = static_cast<BYTE>(b);
		m_CheckCtrlword();
		if ( ! m_isNotifyMode )
		{
			size_t l = m_rs232c.Send(1, &b1);
			if ( l == 1 )
			{
				if ( !m_isCommandOldMode && cmd == 0x4D && (data == 0x08 || data == 0x09) )
				{
					b1 = 0;
					size_t l = m_rs232c.Receive(1, &b1);
					while ( l == 0 )
					{
						::Sleep(0);
						l = m_rs232c.Receive(1, &b1);
					}
					TLOG("       Recv@(0x%02X)", b1);
					if ( b1 == 0xFF && m_datasPortA.GetSize() > 0 )
					{
						TLOG("Send(0xFF) - %d", m_datasPosition);
	                    if (m_lastCmd == 0x4D || m_datasPosition >= 35) // STAT LUSTER 対策
						{
							m_datasPortA.Free();
							TLOG("     ->Free");
						}
					}
				}
				m_lastCmd = cmd;
				return true;
			}
			TLOG("       - SendError1");
		}
		else 
		{
			size_t l = m_rs232c.Send(1, &b1);
			if ( l == 1 )
			{
				loop ( i, 5000000 ) 
				{
					if ( IsRecvEndMark() )
					{
						return true;
					}
					::Sleep(0);
				}
			}
			TLOG("       - Timeout1");
		}
		Stop();
		MessageBox(NULL, "    end 8255 emulation mode 2   ", "XM6", MB_OK);
		return false;
	}

private:

	/**
	 * コントロールワードの送信チェック
	 */
    void m_CheckCtrlword(void)
    {
        if (m_controlWordNext >= 0) 
		{
            m_controlWord = static_cast<BYTE>(m_controlWordNext);
            m_controlWordNext = -1;
			BYTE b = static_cast<BYTE>(((m_controlWord & _BIN(01111000)) >> 1) | (m_controlWord & _BIN(00000011)) | _BIN(01000000));
			size_t l = m_rs232c.Send(1, &b);
			if ( l == 1 )
			{
				loop ( i, 5000000 ) 
				{
					if ( IsRecvEndMark() )
					{
						return;
					}
					::Sleep(0);
				}
			}
		}
    }
	
	/**
	 * COM開始.
	 *	@param port COMポート番号
	 *	@retval true 成功(初期化するまでほんとに使えるかはわからない)
	 *	@retval false 失敗。
	 */
	bool StartCom(int port)
	{
		m_rs232c.SetParameter(port, 0, 8, CRs232c::Parity_Non, CRs232c::StopBits_1);
		return m_rs232c.Open();
	}

	/**
	 * スレッド開始.
	 *	@retval true 成功。
	 *	@retval false 失敗。
	 */
	bool StartThread(void)
	{
		m_thread.SetRunner(this);
		return m_thread.Start();
	}

	/**
	 * 終端受信した？
	 *	@retval true 受信した
	 *	@retval false まだ。
	 */
	volatile bool IsRecvEndMark(void)
	{
		bool r = m_isRecvEndMark;
		if (r) 
		{
			m_isRecvEndMark = false;
		}
		return r;
	}

	/**
	 * RS233Cイベント発生コールバック.
	 *	@return ０
	 */
	virtual DWORD Run(void)
	{
		BYTE b1;
		while ( IsRunnable() )
		{
			size_t l = m_rs232c.Receive(1, &b1);
			if ( l == INVALID_SIZE )
			{
				m_rs232c.Close();
				break;
			}
			else if ( l > 0  )
			{
				if ( (b1 & 0xF0) == 0x30 ) 
				{
					m_recvData = b1;
					m_isRecvEndMark = true;
					TLOG("       - Recv(0x%02X)", b1);
				}
				if ( m_isNotifyMode )
				{
					if ( (b1 & _BIN(10010000)) == _BIN(10000000) )
					{
						// 0x3A
						m_portA = static_cast<BYTE>(b1 | _BIN(10010000));
						TLOG("       - Recv(0x%02X, 0x%02X)", 0x3A, m_portA);
					}
					else if ( (b1 & _BIN(10010000)) == _BIN(10010000) )
					{
						// 0x3B
						m_portB = static_cast<BYTE>(b1 | _BIN(10010000));
						TLOG("       - Recv(0x%02X, 0x%02X)", 0x3B, m_portB);
					}
					else if ( (b1 & _BIN(11110000)) == 0 )
					{
						// 0x3C
						m_portC = static_cast<BYTE>(b1 << 4);
						TLOG("       - Recv(0x%02X, 0x%02X)", 0x3C, m_portC);
					}
				}
			}
			::Sleep(0);
		}
		return 0;
	}

	CWorkMem		m_datasPortA;		///< PortAの先読みDATAs
	size_t			m_datasPosition;	///< DATAsのポジション
	CThread			m_thread;			///< スレッドクラス
	CRs232c			m_rs232c;			///< RS232Cクラス
	bool			m_isNotifyMode;		///< モード。trueなら通知モード, false ならコマンドモード
	volatile bool	m_isRecvEndMark;	///< エンドマーク受信フラグ（通知モード用）
	BYTE			m_recvData;			///< 受信データ（通知モード用）
	volatile BYTE	m_portA;			///< 受信データ（通知モード用）
	volatile BYTE	m_portB;			///< 受信データ（通知モード用）
	volatile BYTE	m_portC;			///< 受信データ（通知モード用）
	BYTE			m_controlWord;		///< 設定したコントロールワード
	int				m_controlWordNext;	///< 設定したコントロールワード
	BYTE			m_lastCmd;
	bool			m_isCommandOldMode;
};

