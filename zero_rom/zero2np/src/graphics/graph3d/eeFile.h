/* ==========================================================================
 *  eeFile.h
 *
 *  Thin C++ file wrapper over the EE sceOpen/sceRead/sceWrite/sceClose/sceLseek
 *  fileio API.  CFile is the base I/O object (handle + error string + cached
 *  file name); CStdioFile adds line-oriented text helpers; CFileName is a
 *  basic_string-derived path splitter (drive / dir / file / ext) used by
 *  CFile::GetFileName / GetFileTitle / GetFilePath.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _EEFILE_H
#define _EEFILE_H

#include <string>
#include <sifdev.h>

/* The project's string type: basic_string<char, string_char_traits<char>,
 * ctl::custom_allocator<char> >.  Treated as std::string here. */
typedef std::string string;

/* --------------------------------------------------------------------------
 *  Open() flags / Seek() origins.
 * ------------------------------------------------------------------------ */
enum OpenFlags
{
    modeRead    = 0x0000,
    modeWrite   = 0x0001,
    modeCreate  = 0x1000,
    RWMASK      = 0x0003,
    WRITEMASK   = 0x1000
};

enum SeekPosition
{
    begin   = 0,
    current = 1,
    end     = 2
};

/* ==========================================================================
 *  CFile -- base file object (sizeof 0x150)
 * ======================================================================== */
class CFile
{
protected:
    string      m_strError;             /* 0x000 last error message */

private:
    int         m_iHandle;              /* 0x004 sceOpen handle, -1 = closed */
    sce_stat    m_Status;               /* 0x008 cached stat block */
    char        m_strFileName[260];     /* 0x048 full path used on Open() */

protected:
    int         OnError(const char *pStr, ...);

public:
    CFile();
    virtual ~CFile();

    char       *ErrorString();

    int          Open(const char *pFileName, unsigned int uiOpenFlags);
    unsigned int Read(void *lpBuf, unsigned int uiCount);
    int          Write(const void *lpBuf, unsigned int uiCount);
    void         Close();

    int          Seek(int iOffset, int iFrom);
    unsigned int GetLength();
    unsigned int SeekToEnd();
    void         SeekToBegin();

    virtual string GetFileName();       /* 0x14c vtbl */
    virtual string GetFileTitle();
    virtual string GetFilePath();
};

/* ==========================================================================
 *  CStdioFile -- adds line-oriented text helpers (sizeof 0x150)
 * ======================================================================== */
class CStdioFile : public CFile
{
public:
    CStdioFile();
    virtual ~CStdioFile();

    virtual void  WriteString(const char *lpsz);
    virtual char *ReadString(char *lpsz, unsigned int nMax);
};

/* ==========================================================================
 *  CFileName -- path splitter derived from string (sizeof 0x24)
 * ======================================================================== */
class CFileName : public string
{
    friend class CFile;

public:
    CFileName() : string() {}
    CFileName(const char *pStr) : string(pStr) {}
    virtual ~CFileName() {}

protected:
    string  m_strDrive;         /* 0x04 */
    string  m_strDir;           /* 0x08 */
    string  m_strFile;          /* 0x0c */
    string  m_strExt;           /* 0x10 */
    string  m_strDriveDir;      /* 0x14 drive + dir */
    string  m_strParentDir;     /* 0x18 */
    string  m_strFileExt;       /* 0x1c file + ext */
                                /* 0x20 vtbl */
};

#endif /* _EEFILE_H */
