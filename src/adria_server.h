#ifndef ADRIA_SERVER_H_
#define ADRIA_SERVER_H_

#include <base.h>
#include <net.h>
#include <mine.h>
#include <thread.h>

namespace TDataAccess {

/////////////////////////////////////////////////////////////////////
// Backup log and backup DB handler
class TDataProvider {
private:
	static int SaveInterval;
	static TStr LogFName;

	const TStr DbFNm;

	TCriticalSection DataSection;

	int NSaves;

	PNotify Notify;

public:
	TDataProvider(const TStr& DbFNm, const PNotify& _Notify);

	virtual ~TDataProvider() {  }

//	static void InitAggregates(TQm::PBase& Base, const PNotify& Notify);
public:
	// stores a new record
	void AddRec(const int& CanId, const PJsonVal& Rec);
	// returns the history of the sensor with the given CAN ID
	void GetHistory(const int& CanId, TUInt64FltPrV& HistoryV);

//	PBase& GetQmBase() { return QmBase; }

private:
//	void InitQmBase();
	void SaveRec(const int& CanId, const PJsonVal& Rec);
//	void AddRecToLog(const int& CanId, const PJsonVal& Rec);
	// fills the records in the active log file to base, saves the base and invalidates the log
//	void UpdateFromLog();
	// invalidates the currently active log file
//	void InvalidateLog();
	// synchronizes QMiners database
//	void SaveBase();
	// moves the backup DB into DB
//	void RestoreBackupDb();

private:
	// helpers
	// removes directory Dest and copies Src into it
	void CopyDir(const TStr& Src, const TStr& Dest);

	// returns the name of the backup databases directory
	TStr GetBackupDbPath() const;
};

}

namespace TAdriaComm {

using namespace TDataAccess;

enum TAdriaMsgMethod {
	ammNone,
	ammPush,
	ammPost,
	ammGet
};

/////////////////////////////////////////////////////////
// Adria Message class
// parses the message from the input stream and holds it's content
class TAdriaMsg;
typedef TPt<TAdriaMsg> PAdriaMsg;
class TAdriaMsg{
private:
  TCRef CRef;
public:
  friend class TPt<TAdriaMsg>;
public:
	const static TChA POST;
	const static TChA PUSH;
	const static TChA GET;

	const static TChA RES_TABLE;
	const static TChA HISTORY;
	const static int BYTES_PER_EL;

private:
	TChA Buff;

	TAdriaMsgMethod Method;
	TChA Command;
	TChA Params;
	TChA ComponentId;
	TInt Length;
	TChA Content;

	bool IsLastReadEol;

	PNotify Notify;

public:
	TAdriaMsg(const PNotify& _Notify=TStdNotify::New()):
		Buff(400), Method(TAdriaMsgMethod::ammNone), Command(TStr()),
		Params(TStr()), Length(-1), IsLastReadEol(true), Notify(_Notify) {}

	static PAdriaMsg New(const PNotify& Notify=TStdNotify::New()) { return new TAdriaMsg(Notify); }

	virtual ~TAdriaMsg() {}

public:
	TStr GetStr() const;

	bool IsComplete() const;
	void Read(const PSIn& In);

	bool IsPush() const { return IsMethod(TAdriaMsgMethod::ammPush); }
	bool IsPost() const { return IsMethod(TAdriaMsgMethod::ammPost); }
	bool IsGet() const { return IsMethod(TAdriaMsgMethod::ammGet); }

	const TChA& GetCommand() const { return Command; }
	const TChA& GetContent() const { return Content; }
	const TChA& GetComponentId() const { return ComponentId; }
	const TChA& GetParams() const { return Params; }

private:
	void ReadUntil(const PSIn& In, const TStr& EndStr, TChA& Out) const;
	void ReadLine(const PSIn& In, TChA& Out) const;

	bool HasMethod() const { return Method != TAdriaMsgMethod::ammNone; }
	bool HasCommand() const { return !Command.Empty(); }
	bool HasComponentId() const { return !ComponentId.Empty(); }
	bool HasParams() const { return !Params.Empty(); }
	bool HasContent() const { return IsPush() || IsPost(); }

	bool IsMethod(const TAdriaMsgMethod& Mtd) const { return Method == Mtd; }
};

class TAdriaMsgCallback;
typedef TPt<TAdriaMsgCallback> PAdriaMsgCallback;
class TAdriaMsgCallback {
private:
	TCRef CRef;
public:
	friend class TPt<TAdriaMsgCallback>;
public:
	virtual void OnMsgReceived(const PAdriaMsg& Msg) = 0;

	virtual ~TAdriaMsgCallback() {}
};


/////////////////////////////////////////////////////////
// Adria socket client
// receives messages and calls the callback
class TAdriaCommunicator : public TSockEvent {
private:
	const TStr Url;
	const int Port;

	PSockHost SockHost;
	PSock Sock;

	PNotify Notify;

	PAdriaMsg CurrMsg;

	TVec<PAdriaMsgCallback> MsgCallbacks;

	TCriticalSection SocketSection;
	TCriticalSection CallbackSection;

	bool IsClosed;

public:
	TAdriaCommunicator(const TStr& _Url, const int& _Port, const PNotify& _Notify=TStdNotify::New());

public:
	static PSockEvent New(const TStr& _Url, const int& _Port, const PNotify& _Notify=TStdNotify::New())
		{ return new TAdriaCommunicator(_Url, _Port, _Notify); }

	~TAdriaCommunicator() { CloseConn(); /*~TSockEvent();*/ }

public:
	TSockEvent& operator=(const TSockEvent&){Fail; return *this;}

public:
	void OnRead(const uint64& SockId, const PSIn& SIn);
	void OnWrite(const uint64& SockId) { Notify->OnNotify(TNotifyType::ntInfo, "Written to socket!"); }
	void OnAccept(const uint64& SockId, const PSock&){Fail;}
	void OnConnect(const uint64& SockId);
	void OnReadEof(const uint64& SockId);
	void OnClose(const uint64& SockId);
	void AfterClose(const uint64& SockId);
	void OnTimeOut(const uint64& SockId);// { OnClose(SockId); }
	void OnError(const uint64& SockId, const int& ErrCd, const TStr& ErrStr);
	void OnGetHost(const PSockHost& SockHost);

	bool Write(const PSIn& SIn);
	bool Write(const TChA& Msg);
	void ShutDown();

public:
	const TStr& GetUrl() const { return Url; }
	const int& GetPort() const { return Port; }

	void AddOnMsgReceivedCallback(const PAdriaMsgCallback& Callback);
	void OnMsgReceived(const PAdriaMsg Msg);

private:
	void Connect();
	void CloseConn();
	void Reconnect();
};




class TAdriaServer;
typedef TPt<TAdriaServer> PAdriaServer;
class TAdriaServer: public TAdriaMsgCallback {
private:
	TCRef CRef;
public:
	friend class TPt<TAdriaServer>;
private:
	TDataProvider DataProvider;
	PSockEvent Communicator;

	PNotify Notify;
public:
	TAdriaServer(const PSockEvent& _Communicator, const TDataProvider& _DataProvider, const PNotify& _Notify = TStdNotify::New());
	static PAdriaServer New(const PSockEvent& _Communicator, const TDataProvider& _DataProvider, const PNotify& _Notify = TStdNotify::New())
		{ return new TAdriaServer(_Communicator, _DataProvider, _Notify); }

	~TAdriaServer() {}

public:
	void OnMsgReceived(const PAdriaMsg& Msg);

	void ShutDown();

private:
	void ParseTable(const TChA& Table, THash<TUInt, TFlt>& CanIdValH);
	void ProcessPushTable(const PAdriaMsg& Msg);
	void ProcessGetHistory(const PAdriaMsg& Msg);
};


}

#endif /* ADRIA_SERVER_H_ */
