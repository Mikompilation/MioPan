/* ==========================================================================
 *  eeFindFile.h
 *
 *  CFindFile -- a directory scanner / file-spec matcher built over the EE
 *  sce directory API (sceDopen / sceDread / sceDclose).  Given a directory
 *  and a '|'-separated list of file specs (each "name.ext" with '*'
 *  wildcards), it enumerates the directory and classifies every entry into
 *  matched files, all files, and sub-directories.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _EEFINDFILE_H
#define _EEFINDFILE_H

#include <string>
#include <vector>
/* No <sys/stat.h>: sce_stat comes from sifdev.h, and glibc's header defines
 * st_ctime/st_atime/st_mtime as macros that rewrite sce_stat's members. */
#include <sifdev.h>             /* sce_stat, sce_dirent, sceDopen/sceDread/sceDclose */

/* The prototype's custom basic_string is treated as std::string here. */
typedef std::string string;

class CFindFile
{
public:
    CFindFile();
    virtual ~CFindFile();

    /* Run a scan against an explicit directory + file-spec. */
    int          Scan(const char *pDirName, const char *pFileSpec);
    /* Re-scan the currently configured directory + file-spec. */
    int          Scan();

    void         SetDirectory(const char *pDirName);
    void         SetFileSpec(const char *pFileSpec);
    void         Clear();

    char        *ErrorString();
    char        *CurrentDirectory();

    /* Files that matched one of the configured specs. */
    int          GetNumMatchFile();
    sce_dirent  &GetMatchFileRef(int iIndex);
    /* Every entry returned by the directory read. */
    int          GetNumFile();
    sce_dirent  &GetFileRef(int iIndex);
    /* Sub-directories (excluding "." and ".."). */
    int          GetNumSubDirectory();
    sce_dirent  &GetSubDirectoryRef(int iIndex);
    /* Every sub-directory (including "." and ".."). */
    int          GetNumSubDirectoryAll();
    sce_dirent  &GetSubDirectoryAllRef(int iIndex);

protected:
    int          OnError(const char *pStr, ...);

protected:
    string                   m_strError;             /* 0x00 */

private:
    int          ScanDirectory();
    int          IsMatchSpec(const char *pFileName, const char *strFileSpec);
    int          AnalyzeSpec(const char *pFileSpec);
    static int   IsDirectory(const sce_dirent *pDE);

private:
    string                   m_strOriginDirectory;   /* 0x04 */
    string                   m_strCurrentDirectory;  /* 0x08 */
    std::vector<string>      m_vstrFileSpec;         /* 0x0c */
    std::vector<sce_dirent>  m_vDirEntry;            /* 0x1c */
    std::vector<sce_dirent>  m_vMatchFile;           /* 0x2c */
    std::vector<sce_dirent>  m_vSubDirectory;        /* 0x3c */
    std::vector<sce_dirent>  m_vSubDirectoryAll;     /* 0x4c */
    /* virtual table pointer at 0x5c */
};

#endif /* _EEFINDFILE_H */
