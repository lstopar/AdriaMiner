#include "adria_server.h"

using namespace TDataAccess;
using namespace TAdriaComm;

/////////////////////////////////////////////////////////////////////
// Data handler
TStr TDataProvider::LogFName = "qm.log";
int TDataProvider::SaveInterval = 1000;

TDataProvider::TDataProvider(const TQmParam& Param, const TFAccess& FAccess, const PNotify& _Notify):
		QmParam(Param),
		QmFAccess(FAccess),
		QmSection(TCriticalSectionType::cstRecursive),
		NSaves(0),
		Notify(_Notify) {

	// initialize QMiner
	InitQmBase();
	UpdateFromLog();

	Notify->OnNotify(TNotifyType::ntInfo, "QMiner initialized!");
}

void TDataProvider::InitAggregates(TQm::PBase& Base, const PNotify& Notify) {
	Notify->OnNotify(TNotifyType::ntInfo, "Initializing aggregates...");

	try {
		const TStr LABStoreNm = Base->GetStoreByStoreId(106)->GetStoreNm();

		TStrPrV FieldInterpolatorPrV;
		FieldInterpolatorPrV.Add(TPair<TStr,TStr>("value", TSignalProc::TPreviousPoint::GetType()));

		TQm::PStreamAggr Aggr = TQm::TStreamAggrs::TResampler::New(
				Base,
				"LABSampler",
				LABStoreNm,
				"timestamp",
				FieldInterpolatorPrV,
				"LABResampled",
				1000*60*30,
				0,
				false
		);

		Base->AddStreamAggr(106, Aggr);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to initialize aggregates!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::AddRec(const int& CanId, const PJsonVal& Rec) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Adding record with CAN id: %d", CanId);

	AddRecToBase(CanId, Rec);
	AddRecToLog(CanId, Rec);

	if (++NSaves % SaveInterval == 0) {
		SaveBase();
		NSaves = 0;
	}
}

void TDataProvider::GetHistory(const int& CanId, TUInt64FltPrV& HistoryV) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Fetching history for CAN: %d", CanId);

	try {
		TLock Lock(QmSection);

		TWPt<TStore> Store = QmBase->GetStoreByStoreId(CanId - 100);

		const int TimeFieldId = Store->GetFieldId("timestamp");
		const int ValFieldId = Store->GetFieldId("value");

		// execute the query
		const uint64 MaxTime = TTm::GetCurUniMSecs();				// the time is in milliseconds
		const uint64 MinTime = MaxTime - 1000L*60L*60L;				// current time minus one hour

		PRecSet RecSet = Store->GetAllRecs();	// TODO optimize

		Notify->OnNotifyFmt(TNotifyType::ntInfo, "Found a total of %d records", RecSet->GetRecs());

		RecSet->FilterByFieldTm(TimeFieldId, MinTime, MaxTime);

		Notify->OnNotifyFmt(TNotifyType::ntInfo, "After filtering %d records", RecSet->GetRecs());

		// extract the records
		int NRecs = RecSet->GetRecs();
		for (int i = 0; i < NRecs; i++) {
			TRec Rec = RecSet->GetRec(i);

			uint64 Time = Rec.GetFieldUInt64(TimeFieldId);
			float Val = Rec.GetFieldFlt(ValFieldId);

			HistoryV.Add(TUInt64FltPr(Time, Val));
		}
	} catch (const PExcept& Except) {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to retrieve history for CAN: %d", CanId);
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::InitQmBase() {
	Notify->OnNotify(TNotifyType::ntInfo, "Initializing QMiner base...");

	TLock Lock(QmSection);

	try {
		QmBase = TQm::TStorage::LoadBase(QmParam.DbFPath, QmFAccess, QmParam.IndexCacheSize, QmParam.DefStoreCacheSize, QmParam.StoreNmCacheSizeH);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntInfo, "Failed to initialize QMiner, retrying using temporary DB...");

		RestoreBackupDb();

		QmBase = TQm::TStorage::LoadBase(
				QmParam.DbFPath,
				QmFAccess, QmParam.IndexCacheSize,
				QmParam.DefStoreCacheSize,
				QmParam.StoreNmCacheSizeH);
	}
}

void TDataProvider::AddRecToBase(const int& CanId, const PJsonVal& Rec) {
	Notify->OnNotify(TNotifyType::ntInfo, "Adding record to base");

	try {
		TLock Lock(QmSection);

		if (QmBase->IsStoreId(CanId)) {
			uint64 RecId = QmBase->AddRec(CanId, Rec);
			Notify->OnNotifyFmt(TNotifyType::ntInfo, "Added record with ID: %d", RecId);
		} else {
			Notify->OnNotifyFmt(TNotifyType::ntInfo, "Unknown store ID: %d", CanId);
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Unable to add record to base!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::AddRecToLog(const int& CanId, const PJsonVal& Rec) {
	Notify->OnNotify(TNotifyType::ntInfo, "Adding record to log");

	try {
		TLock Lock(QmSection);

		TFOut Out(LogFName, true);

		Out.PutStr(Rec->GetObjStr("timestamp"));
		Out.PutCh('\t');
		Out.PutInt(CanId);
		Out.PutCh('\t');
		Out.PutFlt(Rec->GetObjNum("value"));
		Out.PutCh('\n');

		Out.Flush();
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Unable to add record to base!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::UpdateFromLog() {
	Notify->OnNotify(TNotifyType::ntInfo, "Updating base from log");

	try {
		if (TFile::Exists(LogFName)) {
			TLock Lck(QmSection);

			TFIn In(LogFName);
			TStr LineStr;
			while (In.GetNextLn(LineStr)) {
				TStrV SplitV;	LineStr.SplitOnAllCh('\t', SplitV);

				const TStr& DateStr = SplitV[0];
				const int& CanId = SplitV[1].GetInt();
				const float Val = SplitV[2].GetFlt();

				PJsonVal JsonVal = TJsonVal::NewObj();
				JsonVal->AddToObj("timestamp", DateStr);
				JsonVal->AddToObj("value", Val);

				AddRecToBase(CanId, JsonVal);
			}

			SaveBase();
		}
	} catch (const PExcept& Except) {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to fill base with the temp log file: %s", Except->GetMsgStr().CStr());
	}
}

void TDataProvider::InvalidateLog() {
	Notify->OnNotify(TNotifyType::ntInfo, "Invalidating log file...");

	try {
		TLock Lock(QmSection);

		if (TFile::Exists(LogFName)) {
			// rename the file and create a new one
			const TUInt64 Time = TTm::GetCurUniMSecs();
			TFile::Rename(LogFName, TStr("qm-") + Time.GetStr() + ".log");
		}

		TFOut Out(LogFName, false);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to invalidate the log file!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::SaveBase() {
	Notify->OnNotify(TNotifyType::ntInfo, "Saving QMiner base...");

	try {
		TLock Lock(QmSection);

		// save the base
		QmBase.Clr();

		// create a backup
		CopyDir(QmParam.DbFPath, GetBackupDbPath());

		// initialize the base again
		InitQmBase();

		// invalidate the active log
		InvalidateLog();

		Notify->OnNotify(TNotifyType::ntInfo, "Base saved!");
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to save QMiner base!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}


void TDataProvider::RestoreBackupDb() {
	Notify->OnNotify(TNotifyType::ntInfo, "Restoring backup DB...");

	try {
		TLock Lock(QmSection);

		CopyDir(GetBackupDbPath(), QmParam.DbFPath);

		Notify->OnNotify(TNotifyType::ntInfo, "Backup DB restored!");
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to restore temporary database!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::CopyDir(const TStr& Src, const TStr& Dest) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Copying directory %s to %s", Src.CStr(), Dest.CStr());

	namespace fs = boost::filesystem;

	try {
		fs::remove_all(Dest.CStr());
		fs::copy_directory(Src.CStr(), Dest.CStr());

		Notify->OnNotify(TNotifyType::ntInfo, "Directory created, copying files...");

		fs::directory_iterator end;
		for (fs::directory_iterator dir_it(Src.CStr()); dir_it != end; ++dir_it) {
			const fs::directory_entry& dir_entry = *dir_it;

			fs::path path = dir_entry.path();

			TStr FPath = TStr(path.c_str());
			TStr DirName, FName;	FPath.SplitOnLastCh(DirName, '/', FName);

			fs::copy_file(path, (Dest + "/" + FName).CStr());
		}

	} catch (const fs::filesystem_error& Err) {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to copy the dir due to a filesystem error! Code: %d, msg: %s", Err.code().value(), Err.what());
		throw TExcept::New(TStr(Err.what()), "TDataProvider::CopyDir");
	}
}

TStr TDataProvider::GetBackupDbPath() const {
	return QmParam.DbFPath.GetSubStr(0, QmParam.DbFPath.Len()-2) + "-temp/";
}

const TChA TAdriaMsg::POST = "POST";
const TChA TAdriaMsg::PUSH = "PUSH";
const TChA TAdriaMsg::GET = "GET";

const TChA TAdriaMsg::RES_TABLE = "res_table";
const TChA TAdriaMsg::HISTORY = "history";

const int TAdriaMsg::BYTES_PER_EL = 6;

////////////////////////////////////////////////////
// TAdriaMsg
bool TAdriaMsg::IsComplete() const {
	if (!IsLastReadEol) { return false; }
	if (!HasMethod() || !HasCommand()) { return false; }

	if (IsPush() || IsPost()) {
		if (Length < 0) { return false; }

		int RealLen = Content.Len();
		if (RealLen != Length) { return false; }
	}

	return true;
}

void TAdriaMsg::ReadUntil(const PSIn& In, const TStr& EndStr, TChA& Out) const {
	int BuffLen = EndStr.Len();

	const char* Target = EndStr.CStr();
	char* Buff = new char[EndStr.Len()];

	bool DelReached = false;
	while (!DelReached) {
		char Ch = In->GetCh();

		Out += Ch;

		strcpy(Buff, Buff+1);
		Buff[BuffLen - 1] = Ch;

		DelReached = strcmp(Buff, Target) == 0;
	}

	delete Buff;
}

void TAdriaMsg::ReadLine(const PSIn& In, TChA& Out) const {
	ReadUntil(In, "\r\n", Out);

	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Read line: %s", Out.CStr());
}

void TAdriaMsg::Read(const PSIn& SIn) {
	// read the method
	TChA LineBuff;	ReadLine(SIn, LineBuff);

	// ignore the EOL
	LineBuff.DelLastCh();	LineBuff.DelLastCh();

	if (LineBuff.IsPrefix(TAdriaMsg::PUSH)) {
		Method = TAdriaMsgMethod::ammPush;
	} else if (LineBuff.IsPrefix(TAdriaMsg::POST)) {
		Method = TAdriaMsgMethod::ammPost;
	} else if (LineBuff.IsPrefix(TAdriaMsg::GET)) {
		Method = TAdriaMsgMethod::ammGet;
	} else {
		throw TExcept::New(TStr("Invalid protocol method: ") + LineBuff, "TAdriaMsg::Read(const PSIn& SIn)");
	}

	// parse the command
	int SpaceIdx = LineBuff.SearchCh(' ', 3);
	// check if the line has parameters
	int QuestionMrkIdx = LineBuff.SearchCh('?', SpaceIdx+1);
	int AndIdx = LineBuff.SearchCh('&', SpaceIdx+1);

	// multiple cases
	if (QuestionMrkIdx < 0 && AndIdx < 0) {
		// only the command is present
		Command = LineBuff.GetSubStr(SpaceIdx+1, LineBuff.Len());
	} else if (AndIdx < 0) {
		// only the parameters are present
		Command = LineBuff.GetSubStr(SpaceIdx+1, QuestionMrkIdx-1);
		Params = LineBuff.GetSubStr(QuestionMrkIdx+1, LineBuff.Len());
	} else if (QuestionMrkIdx < 0) {
		// only the ID is present
		Command = LineBuff.GetSubStr(SpaceIdx+1, AndIdx-1);
		ComponentId = LineBuff.GetSubStr(AndIdx+1, LineBuff.Len());
	} else {
		// both the parameters and the ID are present
		Command = LineBuff.GetSubStr(SpaceIdx+1, QuestionMrkIdx-1);
		Params = LineBuff.GetSubStr(QuestionMrkIdx+1, AndIdx-1);
		ComponentId = LineBuff.GetSubStr(AndIdx+1, LineBuff.Len());
	}

	if (HasContent()) {
		// parse the length
		LineBuff.Clr();
		ReadLine(SIn, LineBuff);
		// ignore the EOL
		LineBuff.DelLastCh();	LineBuff.DelLastCh();

		TStr LenStr = LineBuff.GetSubStr(7, LineBuff.Len());
		Length = LenStr.GetInt();

		// parse the content
		for (int i = 0; i < Length; i++) {
			Content += SIn->GetCh();
		}

		// newline at the end
		SIn->GetCh();	SIn->GetCh();
	}

	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Read: %s", GetStr().CStr());
}

TStr TAdriaMsg::GetStr() const {
	TChA Res;

	switch (Method) {
	case TAdriaMsgMethod::ammPush:
		Res += TAdriaMsg::PUSH;
		break;
	case TAdriaMsgMethod::ammPost:
		Res += TAdriaMsg::POST;
		break;
	case TAdriaMsgMethod::ammGet:
		Res += TAdriaMsg::GET;
		break;
	default:
		throw TExcept::New("Invalid method!", "TAdriaMsg::GetStr()");
	}

	Res.Push(' ');
	Res += Command;

	if (HasParams()) { Res.Push('?'); Res += Params; }

	Res += "\r\n";

	if (HasContent()) {
		Res += TStr("Length=") + TInt::GetStr(Length) + "\r\n";
		Res += Content + TStr("\r\n");
	}

	return Res;
}


//////////////////////////////////////////////////////////
// Adria Client
TAdriaCommunicator::TAdriaCommunicator(const TStr& _Url, const int& _Port, const PNotify& _Notify):
		TSockEvent(),
		Url(_Url),
		Port(_Port),
		SockHost(NULL),
		Sock(NULL),
		Notify(_Notify),
		CurrMsg(TAdriaMsg::New(_Notify)),
		MsgCallbacks(5,0),
		SocketSection(TCriticalSectionType::cstRecursive),
		CallbackSection(TCriticalSectionType::cstRecursive),
		IsClosed(false) {

	Connect();
}

void TAdriaCommunicator::OnConnect(const uint64& SockId) {
	// compose adria protocol initialization request
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Connected socket: %lu, writing desired inputs...", SockId);

	Write("PUSH res_table|GET history&qminer,qm1\r\n");
}

void TAdriaCommunicator::OnRead(const uint64& SockId, const PSIn& SIn) {
	try {
		TLock Lock(SocketSection);

		Notify->OnNotify(TNotifyType::ntInfo, "OnRead...");
		// parse the protocol
		CurrMsg->Read(SIn);

		if (CurrMsg->IsComplete()) {
			OnMsgReceived(CurrMsg);
		}
	} catch (const PExcept& Except) {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to Read: %s", Except->GetMsgStr().CStr());
		Notify->OnNotify(TNotifyType::ntErr, "Reseting...");
	}
}

void TAdriaCommunicator::OnGetHost(const PSockHost& SockHost) {
	Notify->OnNotify(TNotifyType::ntInfo, "OnGetHost called...");

	if (SockHost->IsOk()) {
		Notify->OnNotify(TNotifyType::ntInfo, "Creating socket...");

		Sock = TSock::New(this);
		Sock->Connect(SockHost, GetPort());

		Notify->OnNotify(TNotifyType::ntInfo, "Socket created!");
	} else {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to connect to host!");
	}
}

void TAdriaCommunicator::OnReadEof(const uint64& SockId) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "End of file: %lu", SockId);
	CloseConn();
}

void TAdriaCommunicator::OnTimeOut(const uint64& SockId) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Socket timed out: %lu", SockId);
	CloseConn();
}

void TAdriaCommunicator::OnClose(const uint64& SockId) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Socket closed: %lu", SockId);
}

void TAdriaCommunicator::AfterClose(const uint64& SockId) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "AfterClose: %lu", SockId);
	Reconnect();
}

void TAdriaCommunicator::OnError(const uint64& SockId, const int& ErrCd, const TStr& ErrStr) {
	Notify->OnNotifyFmt(TNotifyType::ntErr, "Error on socket client: %d, %s", ErrCd, ErrStr.CStr());
	CloseConn();
}


bool TAdriaCommunicator::Write(const PSIn& SIn) {
	try {
		TLock Lock(SocketSection);

		// send to socket
		bool Ok;
		TStr ErrMsg;
		Sock->Send(SIn, Ok, ErrMsg);

		if (!Ok) {
			Notify->OnNotify(TNotifyType::ntErr, ErrMsg);
			Notify->OnNotify(TNotifyType::ntErr, "Should reset socket...");
			return false;
		}

		return true;
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "An exception occurred while writing to socket!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
		return false;
	}
}

bool TAdriaCommunicator::Write(const TChA& Msg) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Writing: %s", Msg.CStr());

	return Write(TMIn::New(Msg));
}

void TAdriaCommunicator::Connect() {
	try {
		Notify->OnNotify(TNotifyType::ntInfo, "Connecting...");

		TSockEvent::Reg(this);
		TSockHost::GetAsyncSockHost(GetUrl(), this);
	} catch (const PExcept& Except) {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to connect: %s", Except->GetMsgStr().CStr());
	} catch (...) {
		Notify->OnNotify(TNotifyType::ntErr, "An unknown exception occurred while connecting!");
		throw TExcept::New("Failed to connect socket!", "TAdriaClient::Connect()");
	}
}

void TAdriaCommunicator::CloseConn() {
	Notify->OnNotify(TNotifyType::ntInfo, "TAdriaClient::CloseConn(): Disconnecting...");

	Sock.Clr();

	Notify->OnNotify(TNotifyType::ntInfo, "TAdriaClient::CloseConn(): Disconnected!");
}

void TAdriaCommunicator::Reconnect() {
	Notify->OnNotify(TNotifyType::ntInfo, "Reseting connection...");

	try {
		TSockEvent::UnReg(this);

		sleep(1);

		Connect();

		Notify->OnNotify(TNotifyType::ntInfo, "Connection reset!");
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "An unknown exception occurred while reconnecting!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaCommunicator::ShutDown() {
	IsClosed = true;
	CloseConn();
}

void TAdriaCommunicator::OnMsgReceived(const PAdriaMsg Msg) {
	TLock Lock(CallbackSection);

	CurrMsg = TAdriaMsg::New(Notify);
	for (int i = 0; i < MsgCallbacks.Len(); i++) {
		MsgCallbacks[i]->OnMsgReceived(Msg);
	}
}

void TAdriaCommunicator::AddOnMsgReceivedCallback(const PAdriaMsgCallback& Callback) {
	TLock Lock(CallbackSection);

	MsgCallbacks.Add(Callback);
}


/////////////////////////////////////////////////////////////////////////////
// Adria - Server
TAdriaServer::TAdriaServer(const PSockEvent& _Communicator, const TDataProvider& _DataProvider, const PNotify& _Notify):
		DataProvider(_DataProvider),
		Communicator(_Communicator),
		Notify(_Notify) {

	((TAdriaCommunicator*) Communicator())->AddOnMsgReceivedCallback(this);
}

void TAdriaServer::OnMsgReceived(const PAdriaMsg& Msg) {
	try {
		Notify->OnNotify(TNotifyType::ntInfo, "Received message in callback...");

		if (Msg->IsPush() && Msg->GetCommand() == TAdriaMsg::RES_TABLE) {
			ProcessPushTable(Msg);
		} else if (Msg->IsGet() && Msg->GetCommand() == TAdriaMsg::HISTORY) {
			ProcessGetHistory(Msg);
		} else {
			Notify->OnNotifyFmt(TNotifyType::ntWarn, "Invalid message: %s", Msg->GetStr().CStr());
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process the received message!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaServer::ShutDown() {
	((TAdriaCommunicator*) Communicator())->ShutDown();
}

void TAdriaServer::ParseTable(const TChA& Table, THash<TUInt, TFlt>& CanIdValH) {
	Notify->OnNotify(TNotifyType::ntInfo, "Parsing CAN table...");

	int NEntries = Table.Len() / TAdriaMsg::BYTES_PER_EL;

	int StartIdx;
	TUInt CanId;
	float Val;
	unsigned char Type;
	for (int EntryIdx = 0; EntryIdx < NEntries; EntryIdx++) {
		StartIdx = EntryIdx*TAdriaMsg::BYTES_PER_EL;

		CanId = TUInt((unsigned char) Table[StartIdx]);
		Type = Table[StartIdx + 1];

		switch (Type) {
		case 0: {
			Val = (float) Table[StartIdx + 2];
			break;
		} case 1: {
			Val = *((float*) (Table.CStr() + StartIdx + 2));
			break;
		} default: {
			throw TExcept::New("Invalid type!!");
		}
		}

		CanIdValH.AddDat(CanId, Val);
	}
}


void TAdriaServer::ProcessPushTable(const PAdriaMsg& Msg) {
	try {
		TStr TimeStr = TTm::GetTmFromMSecs(TTm::GetCurUniMSecs()).GetWebLogDateTimeStr(true, "T");

		// parse the table
		THash<TUInt, TFlt> CanIdValH;
		const TChA& Table = Msg->GetContent();
		ParseTable(Table, CanIdValH);

		TUIntV KeyV;	CanIdValH.GetKeyV(KeyV);
		for (int i = 0; i < KeyV.Len(); i++) {
			TUInt CanId = KeyV[i];
			TFlt Val = CanIdValH(CanId);

			PJsonVal RecJson = TJsonVal::NewObj();
			RecJson->AddToObj("timestamp", TimeStr);
			RecJson->AddToObj("value", Val);

			DataProvider.AddRec(CanId, RecJson);

			Notify->OnNotifyFmt(TNotifyType::ntInfo, "Added record: (%d,%s)\n", CanId.Val, Val.GetStr().CStr());
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process PUSH res_table!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaServer::ProcessGetHistory(const PAdriaMsg& Msg) {
	try {
		const TStr ComponentId = Msg->GetComponentId();
		const TInt CanId = TStr(Msg->GetParams()).GetInt();

		TUInt64FltPrV HistoryV;	DataProvider.GetHistory(CanId, HistoryV);

		int NHist = HistoryV.Len();

		// content
		TChA ContentChA = "";
		for (int i = 0; i < NHist; i++) {
			const TUInt64FltPr& HistEntry = HistoryV[i];

			ContentChA += HistEntry.Val1.GetStr();

			ContentChA += ',';

			ContentChA += HistEntry.Val2.GetStr();

			if (i < NHist-1) {
				ContentChA += ',';
			}
		}

		ContentChA += "\r\n";

		// generate PUSH message
		// first line
		TChA Msg = "PUSH history?";
		Msg += CanId.GetStr();
		Msg += "&";
		Msg += ComponentId;
		Msg += "\r\n";

		// second line
		Msg += "Length=";
		Msg += TInt(ContentChA.Len()-2).GetStr();
		Msg += "\r\n";

		Msg += ContentChA;

		Notify->OnNotifyFmt(TNotifyType::ntInfo, "Sending history response. Number of values: %d", NHist);

		// write the message to the socket
		((TAdriaCommunicator*) Communicator())->Write(Msg);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process GET history!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}
