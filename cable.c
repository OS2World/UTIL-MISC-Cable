/*
 *	Copyright (C) 2010 David Azarewicz
 *	Copyright (C) 2006 Yuri Dario and others.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define  INCL_DOS

#define  INCL_DOSFILEMGR
#define  INCL_DOSERRORS
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <memory.h>
#include <process.h>
#include <math.h>

/* From ioctl.hpp */
#define GENMAC_CATEGORY 		0x99
#define GENMAC_READ_PHY_ADDR	0x01
#define GENMAC_READ_MII_REG 	0x02
#define GENMAC_WRITE_MII_REG	0x03
#define GENMAC_READ_PHY_TYPE	0x04

#define GENMAC_GET_DEBUGLEVEL	0x10
#define GENMAC_SET_DEBUGLEVEL	0x11

#define GENMAC_QUERY_DEV_BUS	0x20

#define GENMAC_HELPER_REQUEST	0x30
#define GENMAC_HELPER_RETURN	0x31

#define GENMAC_WRAPPER_OID_GET	0x40
#define GENMAC_WRAPPER_OID_SET	0x41

/* From poid.hpp */
#define OID_PRIVATE_WRAPPER_LANNUMBER			0xFFFFFF00
#define OID_PRIVATE_WRAPPER_ISWIRELESS			0xFFFFFF01
#define OID_PRIVATE_WRAPPER_LINKSTATUS			0xFFFFFF02
#define OID_PRIVATE_WRAPPER_STATUSCHANGE		0xFFFFFF03
#define OID_PRIVATE_WRAPPER_HALT				0xFFFFFF04
#define OID_PRIVATE_WRAPPER_REINIT				0xFFFFFF05
#define OID_PRIVATE_WRAPPER_ARPLIST 			0xFFFFFF06

/* From xlan\...\ntndis.h */
#define OID_GEN_LINK_SPEED                      0x00010107
#define OID_GEN_MEDIA_CONNECT_STATUS            0x00010114

static char achUsage[] = { "\
CABLE 1.1 (c) 2010 David Azarewicz.\n\
Based on CABLE 1.0 (c) 2006 Yuri Dario.\n\
Portions of code (c) GenMac project and XWP project.\n\
Usage: CABLE DeviceNumber|DriverName [-|ExeName]\n\
DeviceNumber is a GenMac device number, 0 is first genmac device, 1 second, ...\n\
DriverName is a driver name such as NVETH$\n\
ExeName is the name of an executable to start (use - to show status).\n\
If media is disconnected, ExeName will be executed.\n\
"};

//-------------------------------------------------------------------------
int OidGet( char *DevName, int oid, int len, ULONG *Data )
{
	ULONG ParmInOut,DataInOut,Action;
	HFILE GMHandle;
	ULONG Paras[10];
	ULONG openmode;
	APIRET rc;

	openmode = OPEN_FLAGS_NOINHERIT | OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE;

	rc = DosOpen((PSZ)DevName, &GMHandle, &Action, 0, 0, 0x01, openmode, 0);
	if ( rc ) {
		fprintf(stderr,"Open Error : %s (%d)", DevName, rc);
		exit(1);
	}

	Paras[0]  = 0;
	ParmInOut = 10*4;
	DataInOut = len;

	Paras[0] = oid;
	Paras[1] = len;

	memset(Data, 0, len);

	rc = DosDevIOCtl( GMHandle, GENMAC_CATEGORY, GENMAC_WRAPPER_OID_GET,
		&Paras[0], ParmInOut, &ParmInOut, &Data[0], DataInOut, &DataInOut);

	DosClose(GMHandle);

	if (rc) {
		//printf("RC : %d\n",rc);
		return 0;
	}

	//if (Paras[3]) printf("OIDGET : 0x%08x %d %d 0x%08x\n", Paras[0], Paras[1], Paras[2], Paras[3]);

	return Paras[2];
}

//-------------------------------------------------------------------------

/*
 *@@ doshQuickStartSession:
 *		this is a shortcut to DosStartSession w/out having to
 *		deal with all the messy parameters.
 *
 *		This one starts pszPath as a child session and passes
 *		pszParams to it.
 *
 *		In usPgmCtl, OR any or none of the following (V0.9.0):
 *		--	SSF_CONTROL_NOAUTOCLOSE (0x0008): do not close automatically.
 *				This bit is used only for VIO Windowable apps and ignored
 *				for all other applications.
 *		--	SSF_CONTROL_MINIMIZE (0x0004)
 *		--	SSF_CONTROL_MAXIMIZE (0x0002)
 *		--	SSF_CONTROL_INVISIBLE (0x0001)
 *		--	SSF_CONTROL_VISIBLE (0x0000)
 *
 *		Specifying 0 will therefore auto-close the session and make it
 *		visible.
 *
 *		If (fWait), this function will create a termination queue
 *		and not return until the child session has ended. Be warned,
 *		this blocks the calling thread.
 *
 *		Otherwise the function will return immediately.
 *
 *		The session and process IDs of the child session will be
 *		written to *pulSID and *ppid. Of course, in "wait" mode,
 *		these are no longer valid after this function returns.
 *
 *		Returns the error code of DosStartSession.
 *
 *		Note: According to CPREF, calling DosStartSession calls
 *		DosExecPgm in turn for all full-screen, VIO, and PM sessions.
 *
 *@@changed V0.9.0 [umoeller]: prototype changed to include usPgmCtl
 *@@changed V0.9.1 (99-12-30) [umoeller]: queue was sometimes not closed. Fixed.
 *@@changed V0.9.3 (2000-05-03) [umoeller]: added fForeground
 *@@changed V0.9.14 (2001-08-03) [umoeller]: fixed potential queue leak
 *@@changed V0.9.14 (2001-08-03) [umoeller]: fixed memory leak in wait mode; added pusReturn to prototype
 */

APIRET doshQuickStartSession(PCSZ pcszPath, 	  // in: program to start
							 PCSZ pcszParams,	  // in: parameters for program
							 BOOL fForeground,	// in: if TRUE, session will be in foreground
							 USHORT usPgmCtl,	// in: STARTDATA.PgmControl
							 BOOL fWait,		// in: wait for termination?
							 PULONG pulSID, 	// out: session ID (req.)
							 PPID ppid, 		// out: process ID (req.)
							 PUSHORT pusReturn) // out: in wait mode, session's return code (ptr can be NULL)
{
	APIRET arc = NO_ERROR;
	// queue stuff
	const char	*pcszQueueName = "\\queues\\xwphlpsw.que";
	HQUEUE hq = 0;
	PID qpid = 0;

	if (fWait) {
		if (!(arc = DosCreateQueue(&hq, QUE_FIFO | QUE_CONVERT_ADDRESS, (PSZ)pcszQueueName)))
			arc = DosOpenQueue(&qpid, &hq, (PSZ)pcszQueueName);
	}

	if (!arc) {	 // V0.9.14 (2001-08-03) [umoeller]
		STARTDATA	SData;
		CHAR		szObjBuf[CCHMAXPATH];

		SData.Length  = sizeof(STARTDATA);
		SData.Related = SSF_RELATED_INDEPENDENT; //CHILD;
		SData.FgBg	  = (fForeground) ? SSF_FGBG_FORE : SSF_FGBG_BACK;
				// V0.9.3 (2000-05-03) [umoeller]
		SData.TraceOpt = SSF_TRACEOPT_NONE;

		SData.PgmTitle = (PSZ)pcszPath; 	  // title for window
		SData.PgmName = (PSZ)pcszPath;
		SData.PgmInputs = (PSZ)pcszParams;

		SData.TermQ = (fWait) ? (PSZ)pcszQueueName : NULL;
		SData.Environment = 0;
		SData.InheritOpt = SSF_INHERTOPT_PARENT;
		SData.SessionType = SSF_TYPE_DEFAULT;
		SData.IconFile = 0;
		SData.PgmHandle = 0;

		SData.PgmControl = usPgmCtl;

		SData.InitXPos	= 30;
		SData.InitYPos	= 40;
		SData.InitXSize = 200;
		SData.InitYSize = 140;
		SData.Reserved = 0;
		SData.ObjectBuffer	= szObjBuf;
		SData.ObjectBuffLen = (ULONG)sizeof(szObjBuf);

		if ( (!(arc = DosStartSession(&SData, pulSID, ppid))) && (fWait) ) {
			// block on the termination queue, which is written
			// to when the subprocess ends
			REQUESTDATA rqdata;
			ULONG		cbData = 0;
			PULONG		pulData = NULL;
			BYTE		elpri;

			rqdata.pid = qpid;
			if (!(arc = DosReadQueue(hq,				// in: queue handle
									 &rqdata,			// out: pid and ulData
									 &cbData,			// out: size of data returned
									 (PVOID*)&pulData,	// out: data returned
									 0, 				// in: remove first element in queue
									 0, 				// in: wait for queue data (block thread)
									 &elpri,			// out: element's priority
									 0)))				// in: event semaphore to be posted
			{
				if (!rqdata.ulData) {
					// child session ended:
					// V0.9.14 (2001-08-03) [umoeller]

					// *pulSID = (*pulData) & 0xffff;
					if (pusReturn) *pusReturn = ((*pulData) >> 16) & 0xffff;

				}
				// else: continue looping

				if (pulData) DosFreeMem(pulData);
			}
		}
	}

	if (hq) DosCloseQueue(hq);

	return arc;
}

//-------------------------------------------------------------------------

int main(int argc, char **argv)
{
	int iDev, iRc;
	char achDevName[128];
	char achExeName[128];
	ULONG ulTmp;
	ULONG ulIsDisconnected;

	if (argc < 2) {
		printf(achUsage);
		exit(1);
	}

	if (strlen(argv[1]) > 2) {
		strcpy(achDevName, argv[1]);
	} else {
		strcpy( achDevName, "WRND32$") ;
		iDev = atoi( argv[1]);
		if (iDev > 0) sprintf( achDevName, "WRND32%d$", iDev+1);
	}

	*achExeName = 0;
	if (argc >= 3) strcpy(achExeName, argv[2]);
	if (*achExeName == '-') *achExeName = 0;

	iRc = OidGet( achDevName, OID_PRIVATE_WRAPPER_LANNUMBER, sizeof(ulTmp), &ulTmp);
	if (iRc) printf("Lan-Number      : lan%d\n", ulTmp);

	iRc = OidGet( achDevName, OID_GEN_LINK_SPEED, sizeof(ulTmp), &ulTmp);
	if (iRc == 0) {
		printf("The %s driver does not support this function\n", achDevName);
		exit(1);
	}
	printf("LinkSpeed       : %d [Mbit/s]\n", ulTmp*100/1000000);

	iRc = OidGet( achDevName, OID_GEN_MEDIA_CONNECT_STATUS, sizeof(ulIsDisconnected), &ulIsDisconnected);
	if (iRc == 0) {
		printf("The %s driver does not support this function\n", achDevName);
		exit(1);
	}
	printf("LinkStatus      : %sCONNECTED %lX\n", ulIsDisconnected ? "DIS" : "", ulIsDisconnected);
	iRc = 0;
	if (ulIsDisconnected && *achExeName) {
		ULONG 	ulSID;
		PID 	pid;
		USHORT 	usReturn;
		iRc = doshQuickStartSession( achExeName,	   // in: program to start
						 NULL,	   // in: parameters for program
						 TRUE,	// in: if TRUE, session will be in foreground
						 SSF_CONTROL_NOAUTOCLOSE,	// in: STARTDATA.PgmControl
						 FALSE, 	   // in: wait for termination?
						 &ulSID,	 // out: session ID (req.)
						 &pid,		   // out: process ID (req.)
						 &usReturn); // out: in wait mode, session's return code (ptr can be NULL)
		if (iRc) printf("Failed to start %s with rc=%d\n", achExeName, iRc);
	}
	return iRc;
}

