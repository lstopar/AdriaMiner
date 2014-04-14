#ifndef ADRIA_SERVER_H_
#define ADRIA_SERVER_H_

#define BOOST_NO_CXX11_SCOPED_ENUMS

#include <boost/filesystem.hpp>
#include <qminer.h>
#include <qminer_js.h>
#include <base.h>
#include <net.h>
#include <mine.h>
#include <thread.h>

namespace TDataAccess {

using namespace TQm;

class TQmParam {
public:
	// javascript parameters
	class TJsParam {
	public:
		// script namespace
		TStr Nm;
		// script filename
		TStr FNm;
		// script initialization parameters
		PJsonVal InitVal;
		// libraries include path
		TStrV IncludeFPathV;
		// folders with access permissions
		TStrV AccessFPathV;
	private:
		// get qminer global library include folder
		void AddQMinerLibFPath() {
			IncludeFPathV.Add(TQm::TEnv::QMinerFPath + "./lib/");
		}

		// get local include folder if exists
		void AddLocalLibFPath() {
			TStr LibFPath = FNm.GetFPath() + "lib/";
			if (TDir::Exists(LibFPath)) {
				IncludeFPathV.Add(LibFPath);
			}
		}

		// get sandbox folder
		void AddSandboxAccessFPath(const TStr& RootFNm) {
			// handle default access directories only when sandbox exists
			TStr SandboxFPath = TStr::GetNrAbsFPath("sandbox", RootFNm);
			if (TDir::Exists(SandboxFPath)) {
				// prepare script specific space
				TStr AccessFPath = TStr::GetNrAbsFPath(Nm, SandboxFPath);
				// if doesn't exist yet, create it
				if (!TDir::Exists(AccessFPath)) { TDir::GenDir(AccessFPath); }
				// done
				AccessFPathV.Add(AccessFPath);
			}
		}

	public:
		// required by vectors
		TJsParam() { }

		// parse from json configuration file
		TJsParam(const TStr& RootFNm, const PJsonVal& JsVal) {
			EAssertR(JsVal->IsObj(), "Unsupported type: " + TJsonVal::GetStrFromVal(JsVal));
			// we must have at least the script name
			EAssert(JsVal->IsObjKey("file"));
			// get script name
			FNm = JsVal->GetObjStr("file");
			// get namespace (get from script name if not available)
			Nm = JsVal->IsObjKey("name") ? JsVal->GetObjStr("name") : FNm.GetFMid();
			// get initialization parameters (if available)
			InitVal = JsVal->IsObjKey("init") ? JsVal->GetObjKey("init") : TJsonVal::NewObj();
			// get library include folders
			if (JsVal->IsObjKey("include")) {
				PJsonVal IncludesVal = JsVal->GetObjKey("include");
				EAssertR(IncludesVal->IsArr(), "Expected array of strings, not: " + TJsonVal::GetStrFromVal(IncludesVal));
				for (int IncludeN = 0; IncludeN < IncludesVal->GetArrVals(); IncludeN++) {
					PJsonVal IncludeVal = IncludesVal->GetArrVal(IncludeN);
					EAssertR(IncludeVal->IsStr(), "Expected string, not: " + TJsonVal::GetStrFromVal(IncludeVal));
					IncludeFPathV.Add(IncludeVal->GetStr());
				}
			}
			// handle default includes
			AddLocalLibFPath();
			AddQMinerLibFPath();
			// get folders with access permissions
			if (JsVal->IsObjKey("dirs")) {
				PJsonVal DirsVal = JsVal->GetObjKey("dirs");
				EAssertR(DirsVal->IsArr(), "Expected array of strings, not: " + TJsonVal::GetStrFromVal(DirsVal));
				for (int DirN = 0; DirN < DirsVal->GetArrVals(); DirN++) {
					PJsonVal DirVal = DirsVal->GetArrVal(DirN);
					EAssertR(DirVal->IsStr(), "Expected string, not: " + TJsonVal::GetStrFromVal(DirVal));
					AccessFPathV.Add(DirVal->GetStr());
				}
			}
			// add sandbox access
			AddSandboxAccessFPath(RootFNm);
		}

		// parse from script filename, assuming default structure
		TJsParam(const TStr& RootFNm, const TStr& _FNm) {
			// remember script name
			FNm = _FNm;
			// derive namespace from filename
			Nm = FNm.GetFMid();
			// no initialization parameters
			InitVal = TJsonVal::NewObj();
			// handle default includes
			AddLocalLibFPath();
			AddQMinerLibFPath();
			// add sandbox access
			AddSandboxAccessFPath(RootFNm);
		}

	};

public:
	// project root
	TStr RootFPath;
	// lock file
	TStr LockFNm;
	// database path
	TStr DbFPath;
	// server port
	int PortN;
	TStr Host;
	// index cache size
	uint64 IndexCacheSize;
	// default store cache size
	uint64 DefStoreCacheSize;
	// store specific cache sizes
	TStrUInt64H StoreNmCacheSizeH;
	// javascript parameters
	TVec<TJsParam> JsParamV;
	// file serving folders
	TStrPrV WwwRootV;

private:
	void AddWwwRoot(const PJsonVal& WwwVal) {
		WwwRootV.Add(TStrPr(WwwVal->GetObjStr("name"),
			TStr::GetNrAbsFPath(WwwVal->GetObjStr("path"), RootFPath)));
	}

public:
	TQmParam(const TStr& FNm) {
		EAssertR(TFile::Exists(FNm), "Missing configuration file " + FNm);
		// load configuration file
		PJsonVal ConfigVal = TJsonVal::GetValFromSIn(TFIn::New(FNm));
		EAssertR(ConfigVal->IsObj(), "Invalid setting file - not valid JSON");
		// parse out common stuff
		RootFPath = TStr::GetNrFPath(ConfigVal->GetObjStr("directory", TDir::GetCurDir()));
		LockFNm = RootFPath + "./lock";
		DbFPath = ConfigVal->GetObjStr("database", "./db/");
		PortN = TFlt::Round(ConfigVal->GetObjNum("port"));
		Host = ConfigVal->GetObjStr("host", "127.0.0.1");
		// parse out unicode definition file
		TStr UnicodeFNm = ConfigVal->GetObjStr("unicode", TQm::TEnv::QMinerFPath + "./UnicodeDef.Bin");
		if (!TUnicodeDef::IsDef()) { TUnicodeDef::Load(UnicodeFNm); }

		// parse cache
		if (ConfigVal->IsObjKey("cache")) {
			PJsonVal CacheVal = ConfigVal->GetObjKey("cache");
			// parse out index and default store cache sizes
			IndexCacheSize = int64(CacheVal->GetObjNum("index", 1024)) * int64(TInt::Mega);
			DefStoreCacheSize = int64(CacheVal->GetObjNum("store", 1024)) * int64(TInt::Mega);
			// prase out store specific sizes, when available
			if (CacheVal->IsObjKey("stores")) {
				PJsonVal StoreCacheVals = CacheVal->GetObjKey("stores");
				for (int StoreN = 0; StoreN < StoreCacheVals->GetArrVals(); StoreN++) {
					PJsonVal StoreCacheVal = StoreCacheVals->GetArrVal(StoreN);
					TStr StoreName = StoreCacheVal->GetObjStr("name");
					uint64 StoreCacheSize = int64(StoreCacheVal->GetObjNum("size")) * int64(TInt::Mega);
					StoreNmCacheSizeH.AddDat(StoreName, StoreCacheSize);
				}
			}
		} else {
			// default sizes are set to 1GB for index and stores
			IndexCacheSize = int64(1024) * int64(TInt::Mega);
			DefStoreCacheSize = int64(1024) * int64(TInt::Mega);
		}

		// load scripts
		if (ConfigVal->IsObjKey("script")) {
			// we have configuration file, read it
			PJsonVal JsVals = ConfigVal->GetObjKey("script");
			if (JsVals->IsArr()) {
				for (int JsValN = 0; JsValN < JsVals->GetArrVals(); JsValN++) {
					JsParamV.Add(TJsParam(RootFPath, JsVals->GetArrVal(JsValN)));
				}
			} else {
				JsParamV.Add(TJsParam(RootFPath, JsVals));
			}
		} else {
			// no settings for scripts, assume default setting
			TStr SrcFPath = TStr::GetNrAbsFPath("src", RootFPath);
			TFFile File(SrcFPath, ".js", false); TStr SrcFNm;
			while (File.Next(SrcFNm)) {
				JsParamV.Add(TJsParam(RootFPath, SrcFNm));
			}
		}

		// load serving folders
		//TODO: Add to qm config ability to edit this
		if (ConfigVal->IsObjKey("wwwroot")) {
			PJsonVal WwwVals = ConfigVal->GetObjKey("wwwroot");
			if (WwwVals->IsArr()) {
				for (int WwwValN = 0; WwwValN < WwwVals->GetArrVals(); WwwValN++) {
					AddWwwRoot(WwwVals->GetArrVal(WwwValN));
				}
			} else {
				AddWwwRoot(WwwVals);
			}
		}
		// check for folder with admin GUI
		TStr GuiFPath = TStr::GetNrAbsFPath("gui", TQm::TEnv::QMinerFPath);
		if (TDir::Exists(GuiFPath)) {
			WwwRootV.Add(TStrPr("admin", GuiFPath));
		}
        // check for any default wwwroot
        TStr DefaultWwwRootFPath = TStr::GetNrAbsFPath("www", RootFPath);
        if (TDir::Exists(DefaultWwwRootFPath)) {
            WwwRootV.Add(TStrPr("www", DefaultWwwRootFPath));
        }
	}
};

/////////////////////////////////////////////////////////////////////
// Backup log and backup DB handler
class TDataProvider {
private:
	static int SaveInterval;
	static TStr LogFName;

	TQmParam QmParam;
	TFAccess QmFAccess;

	TCriticalSection QmSection;
	PBase QmBase;

	int NSaves;

	PNotify Notify;

public:
	TDataProvider(const TQmParam& Param, const TFAccess& FAccess, const PNotify& _Notify);

	virtual ~TDataProvider() { QmBase.Clr(); }

	static void InitAggregates(TQm::PBase& Base, const PNotify& Notify);
public:
	// stores a new record
	void AddRec(const int& CanId, const PJsonVal& Rec);
	// returns the history of the sensor with the given CAN ID
	void GetHistory(const int& CanId, TUInt64FltPrV& HistoryV);

	PBase& GetQmBase() { return QmBase; }

private:
	void InitQmBase();
	void AddRecToBase(const int& CanId, const PJsonVal& Rec);
	void AddRecToLog(const int& CanId, const PJsonVal& Rec);
	// fills the records in the active log file to base, saves the base and invalidates the log
	void UpdateFromLog();
	// invalidates the currently active log file
	void InvalidateLog();
	// synchronizes QMiners database
	void SaveBase();
	// moves the backup DB into DB
	void RestoreBackupDb();

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

	PBase& GetQmBase() { return DataProvider.GetQmBase(); }

private:
	void ParseTable(const TChA& Table, THash<TUInt, TFlt>& CanIdValH);
	void ProcessPushTable(const PAdriaMsg& Msg);
	void ProcessGetHistory(const PAdriaMsg& Msg);
};


}

#endif /* ADRIA_SERVER_H_ */
