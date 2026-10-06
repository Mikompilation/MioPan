/* ==========================================================================
 *  eeFile.cpp
 *
 *  Thin C++ file wrapper over the EE fileio API.  CFile maps Open/Read/Write/
 *  Close/Seek onto sceOpen/sceRead/sceWrite/sceClose/sceLseek, records the
 *  last error as a string (via OnError), and offers path queries that lean on
 *  the CFileName path splitter.  CStdioFile adds (stubbed) text helpers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "eeFile.h"
#include "g3ddbg.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sif.h>
#include <fileio.h>

/* --------------------------------------------------------------------------
 *  CFile::CFile
 *
 *  Default-construct the (empty) error string, install the vtbl, and mark the
 *  handle closed.
 * ------------------------------------------------------------------------ */
CFile::CFile()
{
    m_iHandle = -1;
}

/* --------------------------------------------------------------------------
 *  CFile::~CFile
 *
 *  Close any open handle, then release the error string.
 * ------------------------------------------------------------------------ */
CFile::~CFile()
{
    Close();
}

/* --------------------------------------------------------------------------
 *  CFile::Open
 *
 *  Translate the public OpenFlags into the sceOpen flag word and open the
 *  file.  On success cache the name; on failure record strerror(errno).
 * ------------------------------------------------------------------------ */
int CFile::Open(const char *pFileName, unsigned int uiOpenFlags)
{
    int iRW;
    int iFlag;

    iRW = 0;

    if ((uiOpenFlags & RWMASK) == modeRead)
    {
        iRW = SCE_RDONLY;
    }
    else if ((uiOpenFlags & RWMASK) == modeWrite)
    {
        iRW = SCE_WRONLY;
    }
    else
    {
        iFlag = SCE_CREAT;
        goto check_create;
    }

    iFlag = iRW | SCE_CREAT;

check_create:
    if ((uiOpenFlags & WRITEMASK) == 0)
    {
        iFlag = iRW;
    }

    m_iHandle = sceOpen(pFileName, iFlag);

    if (m_iHandle < 0)
    {
        return OnError(strerror(abs(m_iHandle)));
    }

    strcpy(m_strFileName, pFileName);
    return 1;
}

/* --------------------------------------------------------------------------
 *  CFile::Read
 * ------------------------------------------------------------------------ */
unsigned int CFile::Read(void *lpBuf, unsigned int uiCount)
{
    int iBytesRead;

    G3DASSERT(m_iHandle >= 0, "m_iHandle:%d(%s)", m_iHandle, strerror(abs(m_iHandle)));

    iBytesRead = sceRead(m_iHandle, lpBuf, uiCount);

    if (iBytesRead < 0)
    {
        OnError(strerror(-iBytesRead));
    }

    return iBytesRead;
}

/* --------------------------------------------------------------------------
 *  CFile::Write
 * ------------------------------------------------------------------------ */
int CFile::Write(const void *lpBuf, unsigned int uiCount)
{
    int iBytesRead;

    G3DASSERT(m_iHandle >= 0, "m_iHandle:%d(%s)", m_iHandle, strerror(abs(m_iHandle)));

    iBytesRead = sceWrite(m_iHandle, lpBuf, uiCount);

    if (iBytesRead < 0)
    {
        return OnError(strerror(abs(iBytesRead)));
    }

    return (iBytesRead != 0);
}

/* --------------------------------------------------------------------------
 *  CFile::Close
 * ------------------------------------------------------------------------ */
void CFile::Close()
{
    if (m_iHandle != -1)
    {
        sceClose(m_iHandle);
        m_iHandle = -1;
    }
}

/* --------------------------------------------------------------------------
 *  CFile::Seek
 * ------------------------------------------------------------------------ */
int CFile::Seek(int iOffset, int iFrom)
{
    G3DASSERT(m_iHandle >= 0, "m_iHandle:%d(%s)", m_iHandle, strerror(abs(m_iHandle)));

    return sceLseek(m_iHandle, iOffset, iFrom);
}

/* --------------------------------------------------------------------------
 *  CFile::GetLength
 *
 *  Probe the size by seeking to the end, then restore the original position.
 * ------------------------------------------------------------------------ */
unsigned int CFile::GetLength()
{
    int iCurPos;
    int iLength;

    G3DASSERT(m_iHandle >= 0, "m_iHandle:%d(%s)", m_iHandle, strerror(abs(m_iHandle)));

    iCurPos = sceLseek(m_iHandle, 0, SCE_SEEK_CUR);
    iLength = sceLseek(m_iHandle, 0, SCE_SEEK_END);
    sceLseek(m_iHandle, iCurPos, SCE_SEEK_SET);

    return iLength;
}

/* --------------------------------------------------------------------------
 *  CFile::SeekToEnd
 * ------------------------------------------------------------------------ */
unsigned int CFile::SeekToEnd()
{
    return Seek(0, end);
}

/* --------------------------------------------------------------------------
 *  CFile::SeekToBegin
 * ------------------------------------------------------------------------ */
void CFile::SeekToBegin()
{
    Seek(0, begin);
}

/* --------------------------------------------------------------------------
 *  CFile::GetFileName
 *
 *  Split the cached path into drive / dir / file / ext (CFileName), then
 *  return "file.ext" (the file part joined with its extension).
 * ------------------------------------------------------------------------ */
string CFile::GetFileName()
{
    CFileName fn(m_strFileName);
    char strDrive[260];
    char strDir[260];
    char strFile[260];
    char strExt[260];
    const char *pColon;
    const char *pLastSlash;
    const char *pLastYen;
    const char *pLastSep;
    const char *pDot;
    const char *strTop;

    memset(strDrive, 0, sizeof(strDrive));
    memset(strDir, 0, sizeof(strDir));
    memset(strFile, 0, sizeof(strFile));
    memset(strExt, 0, sizeof(strExt));

    strTop = fn.c_str();

    /* drive = "X:" if a colon is present */
    pColon = strrchr(strTop, ':');
    strDrive[0] = '\0';
    if (pColon != NULL)
    {
        strncpy(strDrive, strTop, pColon - strTop + 1);
    }

    /* directory = text between the drive and the last path separator */
    pLastSlash = strrchr(strTop, '/');
    pLastYen = strrchr(strTop, '\\');
    pLastSep = (pLastSlash >= pLastYen) ? pLastSlash : pLastYen;

    {
        const char *pDirTop;
        int iLen;

        pDirTop = strTop;
        if (pColon != NULL)
        {
            pDirTop = pColon + 1;
        }

        iLen = 0;
        if (pLastSep != NULL)
        {
            iLen = pLastSep - pDirTop;
        }

        strDir[0] = '\0';
        if (iLen > 0)
        {
            strncpy(strDir, pDirTop, iLen);
            strcat(strDir, "/");
        }
    }

    /* file = text between the last separator and the extension dot */
    pDot = strrchr(strTop, '.');
    {
        const char *pFnameTop;
        int iLen;

        strFile[0] = '\0';
        pFnameTop = strTop;
        if (pLastSep != NULL)
        {
            pFnameTop = pLastSep + 1;
        }

        if (pDot == NULL)
        {
            iLen = strlen(pFnameTop);
        }
        else
        {
            iLen = pDot - pFnameTop;
        }

        strncpy(strFile, pFnameTop, iLen);
    }

    /* ext = ".xxx" */
    strExt[0] = '\0';
    if (pDot != NULL)
    {
        strcpy(strExt, pDot);
    }

    fn.m_strDrive   = strDrive;
    fn.m_strDir     = strDir;
    fn.m_strFile    = strFile;
    fn.m_strExt     = strExt;

    fn.m_strDriveDir = fn.m_strDrive;
    fn.m_strDriveDir += fn.m_strDir;

    fn.m_strFileExt = fn.m_strFile;
    fn.m_strFileExt += fn.m_strExt;

    return fn.m_strFileExt;
}

/* --------------------------------------------------------------------------
 *  CFile::GetFileTitle
 *
 *  As GetFileName, but return only the file part (no extension).
 * ------------------------------------------------------------------------ */
string CFile::GetFileTitle()
{
    CFileName fn(m_strFileName);
    char strDrive[260];
    char strDir[260];
    char strFile[260];
    char strExt[260];
    const char *pColon;
    const char *pLastSlash;
    const char *pLastYen;
    const char *pLastSep;
    const char *pDot;
    const char *strTop;

    memset(strDrive, 0, sizeof(strDrive));
    memset(strDir, 0, sizeof(strDir));
    memset(strFile, 0, sizeof(strFile));
    memset(strExt, 0, sizeof(strExt));

    strTop = fn.c_str();

    pColon = strrchr(strTop, ':');
    strDrive[0] = '\0';
    if (pColon != NULL)
    {
        strncpy(strDrive, strTop, pColon - strTop + 1);
    }

    pLastSlash = strrchr(strTop, '/');
    pLastYen = strrchr(strTop, '\\');
    pLastSep = (pLastSlash >= pLastYen) ? pLastSlash : pLastYen;

    {
        const char *pDirTop;
        int iLen;

        pDirTop = strTop;
        if (pColon != NULL)
        {
            pDirTop = pColon + 1;
        }

        iLen = 0;
        if (pLastSep != NULL)
        {
            iLen = pLastSep - pDirTop;
        }

        strDir[0] = '\0';
        if (iLen > 0)
        {
            strncpy(strDir, pDirTop, iLen);
            strcat(strDir, "/");
        }
    }

    pDot = strrchr(strTop, '.');
    {
        const char *pFnameTop;
        int iLen;

        strFile[0] = '\0';
        pFnameTop = strTop;
        if (pLastSep != NULL)
        {
            pFnameTop = pLastSep + 1;
        }

        if (pDot == NULL)
        {
            iLen = strlen(pFnameTop);
        }
        else
        {
            iLen = pDot - pFnameTop;
        }

        strncpy(strFile, pFnameTop, iLen);
    }

    strExt[0] = '\0';
    if (pDot != NULL)
    {
        strcpy(strExt, pDot);
    }

    fn.m_strDrive   = strDrive;
    fn.m_strDir     = strDir;
    fn.m_strFile    = strFile;
    fn.m_strExt     = strExt;

    fn.m_strDriveDir = fn.m_strDrive;
    fn.m_strDriveDir += fn.m_strDir;

    fn.m_strFileExt = fn.m_strFile;
    fn.m_strFileExt += fn.m_strExt;

    return fn.m_strFile;
}

/* --------------------------------------------------------------------------
 *  CFile::GetFilePath
 *
 *  Return the full path the file was opened with.
 * ------------------------------------------------------------------------ */
string CFile::GetFilePath()
{
    const char *s = m_strFileName;
    return string(s);
}

/* --------------------------------------------------------------------------
 *  CStdioFile::WriteString / ReadString
 *
 *  Empty stubs in this prototype build.
 * ------------------------------------------------------------------------ */
void CStdioFile::WriteString(const char *lpsz)
{
}

char *CStdioFile::ReadString(char *lpsz, unsigned int nMax)
{
    return NULL;
}

/* --------------------------------------------------------------------------
 *  CFile::OnError
 *
 *  Format the message into a 256-byte buffer and store it as the object's
 *  error string; always returns 0 so callers can `return OnError(...)` to
 *  signal failure.
 * ------------------------------------------------------------------------ */
int CFile::OnError(const char *pStr, ...)
{
    char str[256];
    va_list args;

    va_start(args, pStr);
    vsprintf(str, pStr, args);
    va_end(args);

    m_strError = str;
    return 0;
}

/* --------------------------------------------------------------------------
 *  CFile::ErrorString
 *
 *  Return the C-string view of the stored error message.
 * ------------------------------------------------------------------------ */
char *CFile::ErrorString()
{
    return (char *)m_strError.c_str();
}

/* ==========================================================================
 *  Compiler-generated material omitted here:
 *
 *    - basic_string<...>::Rep::clone / ::replace overloads, the
 *      ctl::custom_allocator<char> allocate/deallocate thunks, and the
 *      per-class `type_info function` RTTI helpers, all emitted per
 *      translation unit by GCC 2.96-ee from <string> / ctl headers.
 *    - The CStdioFile and CFileName constructor/destructor thunks
 *      (base-string and member-string sub-objects), generated implicitly.
 *
 *  These are not hand-written source; they are reproduced by simply using the
 *  std::string-derived classes above.
 * ======================================================================== */
