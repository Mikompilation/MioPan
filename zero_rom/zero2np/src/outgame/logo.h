/* ==========================================================================
 *  outgame/logo.h
 *
 *  Boot-logo sequence: the Tecmo and "Project Zero" (ZERO) start-up logos.
 *  Each logo has a texture-memory allocation step (Get*TexMem), an async file
 *  load request / poll pair (*TexLoadReq / *TexLoadWait), a per-frame driver
 *  (LogoMain: fade-in / hold / fade-out with a skip-on-key), and a teardown
 *  (Release*TexMem).  InitLogo / InitLogoCtrl reset the module and per-logo
 *  playback state.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_LOGO_H
#define _OUTGAME_LOGO_H

/* --------------------------------------------------------------------------
 *  Module init / per-logo playback reset.
 * ------------------------------------------------------------------------ */
void InitLogo(void);
void InitLogoCtrl(void);

/* --------------------------------------------------------------------------
 *  Texture-memory allocation (from the outgame load heap) and release.
 * ------------------------------------------------------------------------ */
void GetTecmoLogoTexMem(void);
void GetProjectLogoTexMem(void);
void ReleaseTecmoLogoTexMem(void);
void ReleaseProjectLogoTexMem(void);

/* --------------------------------------------------------------------------
 *  Async texture load request + completion poll.
 * ------------------------------------------------------------------------ */
void TecmoLogoTexLoadReq(void);
void ProjectLogoTexLoadReq(void);
int  TecmoLogoTexLoadWait(void);
int  ProjectLogoTexLoadWait(void);

/* --------------------------------------------------------------------------
 *  Per-frame logo driver.  logo_label: 0 = Tecmo, 1 = Project Zero.  in_time /
 *  wait_time / out_time are the fade-in / hold / fade-out frame counts.
 *  Returns non-zero when the logo has finished (fade-out complete).
 * ------------------------------------------------------------------------ */
int  LogoMain(int logo_label, int in_time, int wait_time, int out_time);

/* --------------------------------------------------------------------------
 *  Language-select driver (prototype stub in this build; always returns 1).
 * ------------------------------------------------------------------------ */
int  LangSelMain(void);

#endif /* _OUTGAME_LOGO_H */
