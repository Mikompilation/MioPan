/* ==========================================================================
 *  eeFindFile.cpp
 *
 *  CFindFile -- directory scanner and file-spec matcher over the EE sce
 *  directory API.  AnalyzeSpec splits a '|'-separated spec list into
 *  m_vstrFileSpec; ScanDirectory reads the configured directory with
 *  sceDopen/sceDread/sceDclose into m_vDirEntry, then sorts entries into
 *  m_vMatchFile (spec-matched files), m_vSubDirectory (real sub-dirs) and
 *  m_vSubDirectoryAll (all sub-dirs).  IsMatchSpec does case-insensitive
 *  filename/extension matching with '*' wildcards.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "eeFindFile.h"
#include "g3ddbg.h"
#include <string>
#include <vector>
#include <algorithm>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sifdev.h>             /* sceDopen / sceDread / sceDclose, sce_dirent */

/* stricmp is the Windows CRT's spelling; POSIX has strcasecmp. */
#if !defined(_WIN32)
#include <strings.h>
#define stricmp strcasecmp
#endif

/* The compiler emits per-translation-unit instantiations of the STL/RTTI
 * support used here -- basic_string::Rep::clone / alloc / replace,
 * vector<string>::_M_insert_aux, vector<sce_dirent>::_M_insert_aux,
 * __uninitialized_copy_aux<>, ctl::custom_allocator<>::custom_allocate /
 * custom_deallocate, and the type_info functions.  Those are template
 * boilerplate (not hand-written) and are represented here by the natural
 * std::string / std::vector member calls; they are not re-emitted. */

namespace ctl
{
template <class T>
struct custom_allocator
{
    T *custom_allocate(size_t _Count);
};
}

/* --------------------------------------------------------------------------
 *  CFindFile::CFindFile
 *
 *  Default-construct the four string members and the five vectors.  All
 *  containers start empty.  (Member sub-object construction is compiler
 *  generated; shown here as the natural default initialisation.)
 * ------------------------------------------------------------------------ */
CFindFile::CFindFile()
    : m_strError()
    , m_strOriginDirectory()
    , m_strCurrentDirectory()
    , m_vstrFileSpec()
    , m_vDirEntry()
    , m_vMatchFile()
    , m_vSubDirectory()
    , m_vSubDirectoryAll()
{
}

/* --------------------------------------------------------------------------
 *  CFindFile::~CFindFile
 *
 *  Destroy every container.  (Element destruction + allocator deallocate is
 *  compiler generated from the vector / string templates.)
 * ------------------------------------------------------------------------ */
CFindFile::~CFindFile()
{
}

/* --------------------------------------------------------------------------
 *  CFindFile::IsMatchSpec
 *
 *  Return non-zero when pFileName matches strFileSpec.  Matching is
 *  case-insensitive.  "." and ".." never match.  The spec is split into a
 *  file part and an extension part on '.'; the source name is likewise split
 *  on the last path separator and the last '.'.  A '*' in either part is a
 *  wildcard for that whole component; "*.*" and "*" match anything.
 * ------------------------------------------------------------------------ */
int CFindFile::IsMatchSpec(const char *pFileName, const char *strFileSpec)
{
    char  strSrcFile[256];
    char  strSrcDot[2];
    char  strSrcExt[256];
    char  strSpecFile[256];
    char  strSpecDot[2];
    char  strSpecExt[256];
    const char *pLastSlash;
    const char *pLastYen;
    const char *pLastSep;
    const char *pDot;
    const char *pFnameTop;

    /* "." never matches. */
    if (stricmp(pFileName, ".") == 0)
    {
        return 0;
    }

    /* ".." never matches. */
    if (stricmp(pFileName, "..") == 0)
    {
        return 0;
    }

    memset(strSrcFile, 0, sizeof(strSrcFile));
    memset(strSrcDot, 0, sizeof(strSrcDot));
    memset(strSrcExt, 0, sizeof(strSrcExt));
    memset(strSpecFile, 0, sizeof(strSpecFile));
    memset(strSpecDot, 0, sizeof(strSpecDot));
    memset(strSpecExt, 0, sizeof(strSpecExt));

    /* Split the spec into "file" "." "ext". */
    sscanf(strFileSpec, "%[^.]%[.]%s", strSpecFile, strSpecDot, strSpecExt);

    /* _splitpath-style split of the source file name into name + ext. */
    strrchr(pFileName, ':');
    pLastSlash = strrchr(pFileName, '/');
    pLastYen   = strrchr(pFileName, '\\');
    pLastSep   = pLastSlash;
    if (pLastYen != NULL && (pLastSlash == NULL || pLastSlash < pLastYen))
    {
        pLastSep = pLastYen;
    }
    pDot = strrchr(pFileName, '.');

    if (strSrcFile != (char *)0)
    {
        pFnameTop = pFileName;
        if (pLastSep != NULL)
        {
            pFnameTop = pLastSep + 1;
        }
        strSrcFile[0] = '\0';
        int iLen;
        if (pDot == NULL)
        {
            iLen = strlen(pFnameTop);
        }
        else
        {
            iLen = (int)(pDot - pFnameTop);
        }
        strncpy(strSrcFile, pFnameTop, iLen);
    }

    if (strSrcExt != (char *)0)
    {
        strSrcExt[0] = '\0';
        if (pDot != NULL)
        {
            strcpy(strSrcExt, pDot);
        }
    }

    /* Strip the leading '.' off the source extension. */
    sscanf(strSrcExt, "%[.]%s", strSrcDot, strSrcExt);

    /* Exact (case-insensitive) match of whole spec against whole name? */
    if (stricmp(strFileSpec, pFileName) == 0)
    {
        return 1;
    }

    /* "*" matches everything. */
    if (strcmp(strFileSpec, "*") == 0)
    {
        return 1;
    }

    /* "*.*" matches everything. */
    if (strcmp(strFileSpec, "*.*") == 0)
    {
        return 1;
    }

    /* Wildcard extension: only the file part needs to match. */
    if (strcmp(strSpecExt, "*") == 0)
    {
        if (stricmp(strSpecFile, strSrcFile) == 0)
        {
            return 1;
        }
    }

    /* File part is not a wildcard and did not match exactly above: fail. */
    if (strcmp(strSpecFile, "*") != 0)
    {
        return 0;
    }

    /* Wildcard file part: only the extension needs to match. */
    if (stricmp(strSpecExt, strSrcExt) != 0)
    {
        return 0;
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  CFindFile::AnalyzeSpec
 *
 *  Clear m_vstrFileSpec, then split pFileSpec on '|' and push each component
 *  spec.  Returns 0 if pFileSpec is NULL, 1 on success.
 * ------------------------------------------------------------------------ */
int CFindFile::AnalyzeSpec(const char *pFileSpec)
{
    char strSpec[256];
    char strOR[2];

    if (pFileSpec == (const char *)0)
    {
        return 0;
    }

    m_vstrFileSpec.clear();

    for (; *pFileSpec != '\0'; pFileSpec = pFileSpec + strlen(strSpec) + strlen(strOR))
    {
        memset(strSpec, 0, sizeof(strSpec));
        memset(strOR, 0, sizeof(strOR));

        if (sscanf(pFileSpec, "%[^|]%[|]", strSpec, strOR) < 1)
        {
            break;
        }

        m_vstrFileSpec.push_back(string(strSpec));
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  CFindFile::IsDirectory
 *
 *  True when the stat mode marks the entry as a directory.
 * ------------------------------------------------------------------------ */
int CFindFile::IsDirectory(const sce_dirent *pDE)
{
    return (int)((pDE->d_stat.st_mode & 0xf000) == 0x1000);
}

/* --------------------------------------------------------------------------
 *  CFindFile::ScanDirectory
 *
 *  Open m_strCurrentDirectory and read every entry into m_vDirEntry.  Then:
 *   - for each configured spec, append spec-matched non-directory entries to
 *     m_vMatchFile;
 *   - append every directory entry to m_vSubDirectoryAll, and every directory
 *     entry other than "." / ".." to m_vSubDirectory.
 *  Returns the running result code (1 on success, OnError() result on error).
 * ------------------------------------------------------------------------ */
int CFindFile::ScanDirectory()
{
    int        iFD;
    int        bRet;
    sce_dirent de;
    int        iRet;
    int        i;
    int        j;
    char      *pDirName;

    iFD = sceDopen(m_strCurrentDirectory.c_str());

    m_vDirEntry.clear();
    m_vMatchFile.clear();
    m_vSubDirectory.clear();
    m_vSubDirectoryAll.clear();

    if (iFD < 0)
    {
        bRet = OnError("%s:%s", strerror(abs(iFD)), m_strCurrentDirectory.c_str());
    }
    else
    {
        bRet = 1;

        while ((iRet = sceDread(iFD, &de)) != 0)
        {
            if (iRet < 0)
            {
                bRet = OnError(strerror(abs(iRet)));
                break;
            }

            m_vDirEntry.push_back(de);
        }

        sceDclose(iFD);

        /* Match every directory entry against every configured spec. */
        for (i = 0; i < (int)m_vstrFileSpec.size(); i = i + 1)
        {
            pDirName = (char *)m_vstrFileSpec[i].c_str();

            for (j = 0; j < (int)m_vDirEntry.size(); j = j + 1)
            {
                if (IsMatchSpec(m_vDirEntry[j].d_name, pDirName) != 0)
                {
                    if (IsDirectory(&m_vDirEntry[j]) == 0)
                    {
                        m_vMatchFile.push_back(m_vDirEntry[j]);
                    }
                }
            }
        }

        /* Collect sub-directories. */
        for (i = 0; i < (int)m_vDirEntry.size(); i = i + 1)
        {
            if (IsDirectory(&m_vDirEntry[i]) != 0)
            {
                m_vSubDirectoryAll.push_back(m_vDirEntry[i]);

                if (stricmp(m_vDirEntry[i].d_name, ".") != 0)
                {
                    if (stricmp(m_vDirEntry[i].d_name, "..") != 0)
                    {
                        m_vSubDirectory.push_back(m_vDirEntry[i]);
                    }
                }
            }
        }
    }

    return bRet;
}

/* --------------------------------------------------------------------------
 *  CFindFile::Scan
 *
 *  Set the directory and spec, then scan.
 * ------------------------------------------------------------------------ */
int CFindFile::Scan(const char *pDirName, const char *pFileSpec)
{
    SetDirectory(pDirName);
    SetFileSpec(pFileSpec);
    return ScanDirectory();
}

/* --------------------------------------------------------------------------
 *  CFindFile::SetDirectory
 *
 *  Store the directory to scan.
 * ------------------------------------------------------------------------ */
void CFindFile::SetDirectory(const char *pDirName)
{
    m_strCurrentDirectory = pDirName;
}

/* --------------------------------------------------------------------------
 *  CFindFile::SetFileSpec
 *
 *  Parse and store the file spec list.
 * ------------------------------------------------------------------------ */
void CFindFile::SetFileSpec(const char *pFileSpec)
{
    AnalyzeSpec(pFileSpec);
}

/* --------------------------------------------------------------------------
 *  CFindFile::Scan
 *
 *  Re-scan the currently configured directory + spec.
 * ------------------------------------------------------------------------ */
int CFindFile::Scan()
{
    return ScanDirectory();
}

/* --------------------------------------------------------------------------
 *  CFindFile::Clear
 *
 *  Reset every string and every container to empty.
 * ------------------------------------------------------------------------ */
void CFindFile::Clear()
{
    m_strOriginDirectory  = "";
    m_strCurrentDirectory = "";
    m_vstrFileSpec.clear();
    m_vDirEntry.clear();
    m_vMatchFile.clear();
    m_vSubDirectory.clear();
    m_vSubDirectoryAll.clear();
    m_strError = "";
}

/* --------------------------------------------------------------------------
 *  CFindFile::OnError
 *
 *  Format the printf-style error message into a 256-byte buffer, store it in
 *  m_strError, and return 0.
 * ------------------------------------------------------------------------ */
int CFindFile::OnError(const char *pStr, ...)
{
    char    str[256];
    va_list ap;

    va_start(ap, pStr);
    vsprintf(str, pStr, ap);
    va_end(ap);

    m_strError = str;
    return 0;
}

/* --------------------------------------------------------------------------
 *  CFindFile::ErrorString
 * ------------------------------------------------------------------------ */
char *CFindFile::ErrorString()
{
    return (char *)m_strError.c_str();
}

/* --------------------------------------------------------------------------
 *  CFindFile::GetNumMatchFile / GetMatchFileRef
 * ------------------------------------------------------------------------ */
int CFindFile::GetNumMatchFile()
{
    return (int)m_vMatchFile.size();
}

sce_dirent &CFindFile::GetMatchFileRef(int iIndex)
{
    return m_vMatchFile[iIndex];
}

/* --------------------------------------------------------------------------
 *  CFindFile::GetNumFile / GetFileRef
 * ------------------------------------------------------------------------ */
int CFindFile::GetNumFile()
{
    return (int)m_vDirEntry.size();
}

sce_dirent &CFindFile::GetFileRef(int iIndex)
{
    return m_vDirEntry[iIndex];
}

/* --------------------------------------------------------------------------
 *  CFindFile::GetNumSubDirectory / GetSubDirectoryRef
 * ------------------------------------------------------------------------ */
int CFindFile::GetNumSubDirectory()
{
    return (int)m_vSubDirectory.size();
}

sce_dirent &CFindFile::GetSubDirectoryRef(int iIndex)
{
    return m_vSubDirectory[iIndex];
}

/* --------------------------------------------------------------------------
 *  CFindFile::GetNumSubDirectoryAll / GetSubDirectoryAllRef
 * ------------------------------------------------------------------------ */
int CFindFile::GetNumSubDirectoryAll()
{
    return (int)m_vSubDirectoryAll.size();
}

sce_dirent &CFindFile::GetSubDirectoryAllRef(int iIndex)
{
    return m_vSubDirectoryAll[iIndex];
}

/* --------------------------------------------------------------------------
 *  CFindFile::CurrentDirectory
 * ------------------------------------------------------------------------ */
char *CFindFile::CurrentDirectory()
{
    return (char *)m_strCurrentDirectory.c_str();
}

/* --------------------------------------------------------------------------
 *  ctl::custom_allocator<sce_dirent>::custom_allocate
 *
 *  The one non-template allocator body that survives at a real address
 *  (0x002db45c): allocate storage for _Count sce_dirent objects.  The other
 *  allocate / deallocate bodies are compiler-instantiated template code and
 *  are not re-emitted here.
 * ------------------------------------------------------------------------ */
template <>
sce_dirent *ctl::custom_allocator<sce_dirent>::custom_allocate(size_t _Count)
{
    return (sce_dirent *)::operator new(_Count * sizeof(sce_dirent));
}
