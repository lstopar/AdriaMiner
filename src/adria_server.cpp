#include "adria_server.h"

using namespace TDataAccess;
using namespace TAdriaComm;

/////////////////////////////////////////////////////////////////////
// TSampleHistThread
//unsigned int TDataProvider::TSampleHistThread::SleepTm = 10*60*1000;
uint64 TDataProvider::TSampleHistThread::SleepTm = 10000;

TDataProvider::TSampleHistThread::TSampleHistThread(TDataProvider* Provider, const PNotify& _Notify):
					DataProvider(Provider), Running(false), Notify(_Notify) {
	Notify->OnNotify(TNotifyType::ntInfo, "SampleHistThread initialized!");
}

void TDataProvider::TSampleHistThread::Run() {

	Running = true;
	TSysProc::Sleep(SleepTm);

	while (Running) {
		DataProvider->UpdateHist();
		DataProvider->MakePredictions();
		TSysProc::Sleep(SleepTm);
	}
}

/////////////////////////////////////////////////////////////////////
// Data handler
TStr TDataProvider::LogFName = "qm.log";
//uint64 TDataProvider::HistDur = 1000*60*60*24*7;
uint64 TDataProvider::HistDur = 1000*60*10;
int TDataProvider::EntryTblLen = 256;
TIntStrH TDataProvider::CanIdVarNmH;
TIntSet TDataProvider::PredCanSet;

bool TDataProvider::FillCanHs() {
	CanIdVarNmH.AddDat(103, "temp_cabin");
	CanIdVarNmH.AddDat(104, "temp_ac");
	CanIdVarNmH.AddDat(106, "battery_ls");
	CanIdVarNmH.AddDat(108, "fresh_water");
	CanIdVarNmH.AddDat(122, "temp_bedroom");
	CanIdVarNmH.AddDat(123, "hum_bedroom");
	CanIdVarNmH.AddDat(147, "temp_ls");
	CanIdVarNmH.AddDat(148, "hum_ls");
	CanIdVarNmH.AddDat(159, "temp_sc");
	CanIdVarNmH.AddDat(160, "hum_sc");

	PredCanSet.AddKey(108);

	return true;
}

bool TDataProvider::Init = TDataProvider::FillCanHs();


TDataProvider::TDataProvider(const PNotify& _Notify):
		EntryTbl(TDataProvider::EntryTblLen, TDataProvider::EntryTblLen),
		HistH(),
		HistThread(),
		DataSection(TCriticalSectionType::cstRecursive),
		HistSection(TCriticalSectionType::cstRecursive),
		Notify(_Notify) {

	InitHist();
	HistThread = new TSampleHistThread(this, Notify);
	HistThread->Start();
	Notify->OnNotify(TNotifyType::ntInfo, "Data provider initialized!");
}

void TDataProvider::AddRec(const int& CanId, const PJsonVal& Rec) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Adding record with CAN id: %d", CanId);

	try {
		TLock Lock(DataSection);

		// put the entry into the state table
		EntryTbl[CanId] = Rec->GetObjNum("value");

		AddRecToLog(CanId, Rec);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Unable to add record to base!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::GetHistory(const int& CanId, TUInt64FltKdV& HistoryV) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Fetching history for CAN: %d", CanId);

	if (!HistH.IsKey(CanId)) {
		Notify->OnNotifyFmt(TNotifyType::ntWarn, "Tried to fetch untracked history: %d. Ignoring!", CanId);
		return;
	}

	try {
		TLock Lck(HistSection);
		HistoryV.AddV(HistH.GetDat(CanId));
	} catch (const PExcept& Except) {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to retrieve history for CAN: %d", CanId);
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::PredictByCan(const int& CanId) {
	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Making prediction for CAN: %d", CanId);

	try {
		TUInt64FltKdV HistV;	GetHistory(CanId, HistV);

		double DValSum = 0.0;
		double DtSum = 0.0;
		int DropCount = 0;

		TUInt64FltKd* PrevTmValKd = NULL;
		const int NHist = HistV.Len();
		for (int i = NHist-1; i >= 0; i--) {
			TUInt64FltKd* CurrTmValKd = &HistV[i];

			if (PrevTmValKd != NULL && CurrTmValKd->Dat <= PrevTmValKd->Dat * 1.05 /* threshold */) {
				double Dt = double(CurrTmValKd->Key - PrevTmValKd->Key) / (1000*60*60);	// dt in hours
				double DVal = CurrTmValKd->Dat - PrevTmValKd->Dat;

				DValSum += DVal;
				DtSum += Dt;
				DropCount++;
			}

			PrevTmValKd = CurrTmValKd;
		}

		double DValAvg = DValSum / DtSum;
		double CurrVal = EntryTbl[CanId];

		double HoursLeft = -CurrVal / DValAvg;
		PredictionCallback->OnPrediction(CanId, HoursLeft);
	} catch (const PExcept& Except) {
		Notify->OnNotifyFmt(TNotifyType::ntErr, "Failed to make a prediction for CAN: %d", CanId);
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}


void TDataProvider::AddRecToLog(const int& CanId, const PJsonVal& Rec) {
	Notify->OnNotify(TNotifyType::ntInfo, "Adding record to log");

	try {
		TLock Lock(DataSection);

		bool LogExists = TFile::Exists(LogFName);
		TFOut Out(LogFName, true);

		if (LogExists) { Out.PutCh('\n'); }
		Out.PutStr(Rec->GetObjStr("timestamp"));
		Out.PutCh(',');
		Out.PutInt(CanId);
		Out.PutCh(',');
		Out.PutFlt(Rec->GetObjNum("value"));

		Out.Flush();
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Unable to add record to base!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::UpdateHistFromV(const TFltV& StateV, const uint64& SampleTm) {
	Notify->OnNotify(TNotifyType::ntInfo, "Adding state vector to history...");

	try {
		TLock Lck(HistSection);

		// add the current values to history
		TIntV KeyV;	HistH.GetKeyV(KeyV);

		for (int KeyIdx = 0; KeyIdx < KeyV.Len(); KeyIdx++) {
			const int& CanId = KeyV[KeyIdx];
			const TFlt Val = StateV[CanId];
			HistH.GetDat(CanId).Ins(0, TUInt64FltKd(SampleTm, Val));
		}

		// remove the outdated entries
		for (int KeyIdx = 0; KeyIdx < KeyV.Len(); KeyIdx++) {
			const int& CanId = KeyV[KeyIdx];

			TUInt64FltKdV& HistV = HistH.GetDat(CanId);
			while (HistV.Last().Key < SampleTm - HistDur) {
				HistV.DelLast();
			}
		}

	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to update history!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::UpdateHist() {
	Notify->OnNotify(TNotifyType::ntInfo, "Adding current state to history...");
	UpdateHistFromV(EntryTbl, TTm::GetCurUniMSecs());
}

void TDataProvider::MakePredictions() {
	Notify->OnNotify(TNotifyType::ntInfo, "Making predictions and distributing...");

	try {
		int CurrKeyId = PredCanSet.FFirstKeyId();
		while (PredCanSet.FNextKeyId(CurrKeyId)) {
			PredictByCan(PredCanSet[CurrKeyId]);
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to make predictions!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TDataProvider::InitHist() {
	Notify->OnNotify(TNotifyType::ntInfo, "Initializing history...");

	try {
		TLock Lck(HistSection);

		// init the history hash
		TIntV KeyV;	TDataProvider::CanIdVarNmH.GetKeyV(KeyV);
		for (int i = 0; i < KeyV.Len(); i++) {
			int CanId = KeyV[i];
			HistH.AddDat(CanId, TUInt64FltKdV());
		}

		PSIn SIn = TFIn::New(LogFName);

		TFltV StateTblTemp(TDataProvider::EntryTblLen, TDataProvider::EntryTblLen);

		uint64 CurrSampleTm = TTm::GetCurUniMSecs() - TDataProvider::HistDur;	// time of the next history entry

		TStr Ln;
		while (SIn->GetNextLn(Ln)) {
			TStrV LineStrV;	Ln.SplitOnAllCh(',', LineStrV, true);

			uint64 RecTm = TTm::GetMSecsFromTm(TTm::GetTmFromWebLogDateTimeStr(LineStrV[0], '-', ':', '.', 'T'));
			TInt CanId = LineStrV[1].GetInt();
			TFlt Val = LineStrV[2].GetFlt();

			StateTblTemp[CanId] = Val;

			if (RecTm > CurrSampleTm) {
				// write the samples to history and set new sample time
				UpdateHistFromV(StateTblTemp, CurrSampleTm);
				CurrSampleTm += TDataProvider::TSampleHistThread::SleepTm;
			}
		}

		Notify->OnNotify(TNotifyType::ntInfo, "History initialized!");
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to initialize history!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}


const TChA TAdriaMsg::POST = "POST";
const TChA TAdriaMsg::PUSH = "PUSH";
const TChA TAdriaMsg::GET = "GET";

const TChA TAdriaMsg::RES_TABLE = "res_table";
const TChA TAdriaMsg::HISTORY = "history";
const TChA TAdriaMsg::PREDICTION = "prediction";

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
	const int BuffLen = EndStr.Len();

	const char* Target = EndStr.CStr();
	char* Buff = new char[BuffLen];

	while (!In->Eof()) {
		char Ch = In->GetCh();

		Out += Ch;

		// shift buffer
		for (int i = 0; i < BuffLen-1; i++) { Buff[i] = Buff[i+1]; }
		Buff[BuffLen - 1] = Ch;

		// check if the delimiter was reached
		if (TAdriaMsg::BuffsEq(Buff, Target, BuffLen))
			break;
	}

	delete Buff;
}

void TAdriaMsg::ReadLine(const PSIn& In, TChA& Out) const {
	Notify->OnNotify(TNotifyType::ntInfo, "Reading line...");

	ReadUntil(In, "\r\n", Out);

	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Read line: %s", Out.CStr());
}

void TAdriaMsg::Read(const PSIn& SIn) {
	Notify->OnNotify(TNotifyType::ntInfo, "Parsing method...");

	// read the method
	TChA LineBuff;
	Notify->OnNotify(TNotifyType::ntInfo, "Created line buff...");
	ReadLine(SIn, LineBuff);

	Notify->OnNotifyFmt(TNotifyType::ntInfo, "Received line with length: %d, deleting last two characters...", LineBuff.Len());

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

	Notify->OnNotify(TNotifyType::ntInfo, "Parsing command...");
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
		Notify->OnNotify(TNotifyType::ntInfo, "Parsing params...");
		// only the parameters are present
		Command = LineBuff.GetSubStr(SpaceIdx+1, QuestionMrkIdx-1);
		Params = LineBuff.GetSubStr(QuestionMrkIdx+1, LineBuff.Len());
	} else if (QuestionMrkIdx < 0) {
		// only the ID is present
		Notify->OnNotify(TNotifyType::ntInfo, "Parsing ID...");
		Command = LineBuff.GetSubStr(SpaceIdx+1, AndIdx-1);
		ComponentId = LineBuff.GetSubStr(AndIdx+1, LineBuff.Len());
	} else {
		Notify->OnNotify(TNotifyType::ntInfo, "Parsing params and ID...");
		// both the parameters and the ID are present
		Command = LineBuff.GetSubStr(SpaceIdx+1, QuestionMrkIdx-1);
		Params = LineBuff.GetSubStr(QuestionMrkIdx+1, AndIdx-1);
		ComponentId = LineBuff.GetSubStr(AndIdx+1, LineBuff.Len());
	}

	Notify->OnNotify(TNotifyType::ntInfo, "Parsing content...");
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

	Write("PUSH res_table|GET history,prediction&qminer,qm1\r\n");
	Write("GET res_table\r\n");										// refresh the table
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
TAdriaServer::TAdriaServer(const PSockEvent& _Communicator, TDataProvider& _DataProvider, const PNotify& _Notify):
		DataProvider(_DataProvider),
		Communicator(_Communicator),
		Notify(_Notify) {

	((TAdriaCommunicator*) Communicator())->AddOnMsgReceivedCallback(this);
	DataProvider.SetPredictionCallback(this);
}

void TAdriaServer::OnMsgReceived(const PAdriaMsg& Msg) {
	try {
		Notify->OnNotify(TNotifyType::ntInfo, "Received message in callback...");

		if (Msg->IsPush() && Msg->GetCommand() == TAdriaMsg::RES_TABLE) {
			ProcessPushTable(Msg);
		} else if (Msg->IsGet() && Msg->GetCommand() == TAdriaMsg::HISTORY) {
			ProcessGetHistory(Msg);
		} else if (Msg->IsGet() && Msg->GetCommand() == TAdriaMsg::PREDICTION && Msg->HasParams()) {
			ProcessGetPrediction(Msg);
		} else {
			Notify->OnNotifyFmt(TNotifyType::ntWarn, "Invalid message: %s", Msg->GetStr().CStr());
		}
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process the received message!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

void TAdriaServer::OnPrediction(const TInt& CanId, const TFlt& Val) {
	try {
		TStr ValStr = Val.GetStr();

		TChA Msg = "PUSH prediction?" + CanId.GetStr() +
				"\r\nLength=" + TInt(ValStr.Len()).GetStr() +
				"\r\n" + ValStr + "\r\n";

		((TAdriaCommunicator*) Communicator())->Write(Msg);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process prediction callback!");
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

		TUInt64FltKdV HistoryV;	DataProvider.GetHistory(CanId, HistoryV);

		int NHist = HistoryV.Len();

		// content
		TChA ContentChA = "";
		for (int i = 0; i < NHist; i++) {
			const TUInt64FltKd& HistEntry = HistoryV[i];

			ContentChA += HistEntry.Key.GetStr();

			ContentChA += ',';

			ContentChA += HistEntry.Dat.GetStr();

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

void TAdriaServer::ProcessGetPrediction(const PAdriaMsg& Msg) {
	try {
		const TInt CanId = TStr(Msg->GetParams()).GetInt();
		DataProvider.PredictByCan(CanId);
	} catch (const PExcept& Except) {
		Notify->OnNotify(TNotifyType::ntErr, "Failed to process GET prediction!");
		Notify->OnNotify(TNotifyType::ntErr, Except->GetMsgStr());
	}
}

bool TAdriaMsg::BuffsEq(const char* Buff1, const char* Buff2, const int& BuffLen) {
	for (int i = 0; i < BuffLen; i++) {
		if (Buff1[i] != Buff2[i])
			return false;
	}
	return true;
}
