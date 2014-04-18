#include <qminer.h>
#include "adria_server.h"

const TStr TempLogFName = "unsaved.log";

PNotify Notify = TStdNotify::New();
TAdriaComm::PAdriaServer AdriaServer;

// initialize javascript
void InitJs(const TDataAccess::TQmParam& Param, const TWPt<TQm::TBase>& Base, TVec<TQm::PScript>& ScriptV) {
	for (int JsN = 0; JsN < Param.JsParamV.Len(); JsN++) {
		const TDataAccess::TQmParam::TJsParam& JsParam = Param.JsParamV[JsN];
		TQm::InfoLog("Loading script " + JsParam.FNm.GetFMid() + "...");
        try {
            // initialize javascript engine
            TVec<TQm::TJsFPath> JsFPathV; TQm::TJsFPath::GetFPathV(JsParam.AccessFPathV, JsFPathV);
            TQm::PScript Script = TQm::TScript::New(Base, JsParam.Nm,
                JsParam.FNm, JsParam.IncludeFPathV, JsFPathV);
            // remember the context
            ScriptV.Add(Script);
            // done
            TQm::InfoLog("  done");
        } catch (PExcept& Except) {
            TQm::ErrorLog("Error loading script " + JsParam.FNm.GetFMid() + ":");
            TQm::ErrorLog("  " + Except->GetMsgStr());
        }
	}
}

bool StopSrv() {
	Notify->OnNotify(TNotifyType::ntInfo, "Stopping server...");

	AdriaServer->ShutDown();

	TLoop::Stop();

	Notify->OnNotify(TNotifyType::ntInfo, "Server stopped!");

	return true;
}

void HandleSignal(int Sig) {
	printf("Received signal: %d", Sig);

	switch (Sig) {
	case SIGTERM: {
		StopSrv();
		break;
	case SIGINT: {
		StopSrv();
		break;
	} case SIGHUP: {
		StopSrv();
		break;
	} case SIGSTOP: {
		StopSrv();
		break;
	} default:
		Notify->OnNotifyFmt(TNotifyType::ntInfo, "Received unknown signal: %d", Sig);
	}
	}
}

void RegSigHandlers(const PNotify& Notify) {

	// register signal handlers
	Notify->OnNotify(TNotifyType::ntInfo, "Registering signal handlers...");

	signal(SIGTERM, HandleSignal);
	signal(SIGINT, HandleSignal);
	signal(SIGHUP, HandleSignal);
	signal(SIGSTOP, HandleSignal);
}

void BuildUnicode() {
	printf("Building unicode DB...\n");
	TUniChDb ChDb;
	ChDb.LoadTxt("/home/lstopar/workspace/JSI/qminer/glib/bin/UnicodeDefTxt");
	ChDb.SaveBin("/home/lstopar/workspace/JSI/qminer/UnicodeDef.Bin");
	printf("Unicode complete!\n");
}

/*
 * Starts QMiner
 */
int InitQm(int argc, char* argv[]) {
    PNotify Notify = TStdNotify::New();
	RegSigHandlers(Notify);

	// initialize QMiner environment
	TQm::TEnv::Init();

	// create app environment
	Env = TEnv(argc, argv, TNotify::StdNotify);
	Env.SetNoLine(); // making output prettier
	// command line parameters
	Env.PrepArgs("QMiner " + TQm::TEnv::GetVersion(), 0);
	// read the action
	const bool ConfigP = Env.IsArgStr("config");
	const bool CreateP = Env.IsArgStr("create");
	const bool StartP = Env.IsArgStr("start");
	const bool StopP = Env.IsArgStr("stop");
	const bool ReloadP = Env.IsArgStr("reload");
	const bool DebugP = Env.IsArgStr("debug");
	// stop if no action given
	const bool ActionP = (ConfigP || CreateP || StartP || StopP || ReloadP || DebugP);
	// provide basic instruction when no action given
	if (!ActionP) {
		printf("\n");
		printf("Usage: qm ACTION [OPTION]...\n");
		printf("\n");
		printf("Actions: config, create, start, stop, reload, debug\n");
	} else {
		Env.SetSilent();
	}

	// configuration file
	const TStr ConfFNm = Env.GetIfArgPrefixStr("-conf=", "qm.conf", "Configration file");
	// read config-specific parameters
	if (!Env.IsSilent()) { printf("\nConfiguration parameters:\n"); }
	const int PortN = Env.GetIfArgPrefixInt("-port=", 8080, "Port number");
	const int CacheSizeMB = Env.GetIfArgPrefixInt("-cache=", 1024, "Cache size");
	const bool OverwriteP = Env.IsArgStr("-overwrite", "Overwrite existing configuration file");
	// read create-specific parameters
	if (!Env.IsSilent()) { printf("\nCreate parameters:\n"); }
	const TStr SchemaFNm = Env.GetIfArgPrefixStr("-def=", "", "Store definition file");
	// read start-specific parameters
	if (!Env.IsSilent()) { printf("\nStart parameters:\n"); }
	const bool RdOnlyP = Env.IsArgStr("-rdonly", "Open database in Read-only mode");
//	const bool NoLoopP = Env.IsArgStr("-noserver", "Do not start server after script execution");
	// read stop-specific parameters
	if (!Env.IsSilent()) { printf("\nStop parameters:\n"); }

	// read reload-specific parameters
	if (!Env.IsSilent()) { printf("\nReload parameters:\n"); }
	TStrV ReloadNmV = Env.GetIfArgPrefixStrV("-name=", "Script name");
	// read debug request parameters
	if (!Env.IsSilent()) { printf("\nDebug parameters:\n"); }
	TStr DebugFNm = Env.GetIfArgPrefixStr("-prefix=", "Debug-", "Prefix of debug output files");
	TStrV DebugTaskV = Env.GetIfArgPrefixStrV("-task=", "Debug tasks [indexvoc, index, stores, <store>, <store>_ALL]");
	// read logging specific parameters
	if (!Env.IsSilent()) { printf("\nLogging parameters:\n"); }
	TStr LogFPath = Env.GetIfArgPrefixStr("-log=", "std", "Log Folder (std for standard output, null for silent)");
	const bool Verbose = Env.IsArgStr("-v", "Verbose output (used for debugging)");
	if (!Env.IsSilent()) { printf("\n"); }

	// stop if no action specified
	if (!ActionP) { return 0; }

	// initialize notifier
	TQm::TEnv::InitLogger(Verbose ? 2 : 1, LogFPath, true);
	printf("\n");

	// Create directory structure with basic qm.conf file
	if (ConfigP) {
		// check so we don't overwrite any existing configuration file
		if (TFile::Exists(ConfFNm) && ! OverwriteP) {
			TQm::InfoLog("Configuration file already exists (" + ConfFNm + ")");
			TQm::InfoLog("Use -overwrite to force overwrite");
			return 2;
		}
		// create configuration file
		PJsonVal ConfigVal = TJsonVal::NewObj();
		ConfigVal->AddToObj("port", PortN);
		PJsonVal CacheVal = TJsonVal::NewObj();
		CacheVal->AddToObj("index", CacheSizeMB);
		CacheVal->AddToObj("store", CacheSizeMB);
		ConfigVal->AddToObj("cache", CacheVal);
		// save configuration file
		ConfigVal->SaveStr().SaveTxt(ConfFNm);
		// make folders if needed
		if (!TFile::Exists("db")) { TDir::GenDir("db"); }
		if (!TFile::Exists("src")) { TDir::GenDir("src"); }
		if (!TFile::Exists("src/lib")) { TDir::GenDir("src/lib"); }
		if (!TFile::Exists("sandbox")) { TDir::GenDir("sandbox"); }
	}

	// parse configuration file
	TDataAccess::TQmParam Param(ConfFNm);
	// prepare lock
	TFileLock Lock(Param.LockFNm);

	// Initialize empty database
	if (CreateP) {
		// do not mess with folders with existing running qminer instance
		Lock.Lock();
		{
			BuildUnicode();
			// parse schema (if no given, create an empty array)
			PJsonVal SchemaVal = SchemaFNm.Empty() ? TJsonVal::NewArr() :
				TJsonVal::GetValFromStr(TStr::LoadTxt(SchemaFNm));
			// initialize base
			TQm::PBase Base = TQm::TStorage::NewBase(Param.DbFPath, SchemaVal, 16, 16);
			// initialize aggregates
			TAdriaComm::TDataProvider::InitAggregates(Base, Notify);
			// save base
			TQm::TStorage::SaveBase(Base);
		}
		// remove lock
		Lock.Unlock();
	}

	// Start QMiner engine
	if (StartP) {
		// do not mess with folders with running qminer instance
//		Lock.Lock();
		// load database and start the server
		try {
			// resolve access type
			TFAccess FAccess = RdOnlyP ? faRdOnly : faUpdate;



			// start server
			Notify->OnNotifyFmt(TNotifyType::ntInfo, "Starting socket client, host: %s, port %d", Param.Host.CStr(), Param.PortN);

			AdriaServer = TAdriaComm::TAdriaServer::New(TAdriaComm::TAdriaCommunicator::New(Param.Host, Param.PortN, Notify), TAdriaComm::TDataProvider(Param, FAccess, Notify), Notify);

			// initialize javascript contexts
//			TVec<TQm::PScript> ScriptV; InitJs(Param, DataProvider.GetQmBase(), ScriptV);

			TLoop::Ref();
			TLoop::Run();

			Notify->OnNotify(TNotifyType::ntInfo, "Saving base...");

			// save base
			TQm::TStorage::SaveBase(AdriaServer->GetQmBase());

			Notify->OnNotify(TNotifyType::ntInfo, "Saved!");
		} catch (const PExcept& Except) {
			Notify->OnNotifyFmt(TNotifyType::ntErr, "An exception occurred while initializing: %s", Except->GetMsgStr().CStr());
		} catch (...) {
			Notify->OnNotify(TNotifyType::ntErr, "An unknown exception occurred while initializing!");
		}
		// remove lock
//		Lock.Unlock();
	}

	// Stop QMiner engine
	if (StopP) {
		StopSrv();
	}

	// Debug dumps of database and index
	if (DebugP) {
		// do not mess with folders with existing running qminer instance
		Lock.Lock();
		{
			// load base
			TQm::PBase Base = TQm::TStorage::LoadBase(Param.DbFPath, faRdOnly,
				Param.IndexCacheSize, Param.DefStoreCacheSize, Param.StoreNmCacheSizeH);
			// go over task lists and prepare outputs
			for (int TaskN = 0; TaskN < DebugTaskV.Len(); TaskN++) {
				TStr Task = DebugTaskV[TaskN];
				if (Task == "index") {
					Base->PrintIndex(DebugFNm + "index.txt", true);
				} else if (Task == "indexvoc") {
					Base->PrintIndexVoc(DebugFNm + "indexvoc.txt");
				} else if (Task == "stores") {
					Base->PrintStores(DebugFNm + "stores.txt");
				} else if (Task.IsSuffix("_ALL")) {
					TStr StoreNm = Task.LeftOfLast('_');
					Base->GetStoreByStoreNm(StoreNm)->PrintAll(Base, DebugFNm + Task + ".txt");
				} else if (Base->IsStoreNm(Task)) {
					Base->GetStoreByStoreNm(Task)->PrintTypes(Base, DebugFNm + Task + ".txt");
				} else {
					TQm::InfoLog("Unkown debug task '" + Task + "'");
				}
			}
		}
		// remove lock
		Lock.Unlock();
	}

	Notify->OnNotify(TNotifyType::ntInfo, "Exited!");

	return 0;
}


int main(int argc, char* argv[]) {
#ifndef NDEBUG
    // report we are running with all Asserts turned on
    printf("*** Running in debug mode ***\n");
    setbuf(stdout, NULL);
#endif    
    try {
    	// start QMiner
        int QmSucc = InitQm(argc, argv);
        if (QmSucc != 0) {
        	Notify->OnNotify(TNotifyType::ntErr, "Failed to start QMiner!");
        	return 3;
        }
    } catch (const PExcept& Except) {
    	Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
    	return 1;
    } catch (...) {
    	Notify->OnNotify(TNotifyType::ntErr, "Unknown exception, exiting...");
    	return 2;
    }

    Notify->OnNotify(TNotifyType::ntInfo, "Shut down!");

    return 0;
}
