# 3ds-vgmstream
Port of vgmstream for the nintendo 3ds along with a player

## Usage
1. Create a folder named music at the top level of your sd card.
2. Place any tested file formats in this directory.
3. When the app is opened you are presented with a list of the files it found in the music folder.  Select one and press A.
4. The music will start playing press B to choose something else to play and START to exit.

## Known Issues
See the Issues list for a list of bugs I have already found.
Feel free to report more!

## Supported and Tested Formats
.strm
.brstm

## Theoretically Supported Formats 

Note this list is copied from kode54/vgmstream
And note that while vgmstream supports these formats, this doesn't mean that it will load correctly on the 3ds!

PS2/PSX ADPCM:
- .ads/.ss2
- .ass
- .ast
- .bg00
- .bmdx
- .ccc
- .cnk
- .dxh
- .enth
- .fag
- .filp
- .gcm
- .gms
- .hgc1
- .ikm
- .ild
- .ivb
- .joe
- .kces
- .khv
- .leg
- .mcg
- .mib, .mi4 (w/ or w/o .mih)
- .mic
- .mihb (merged mih+mib)
- .msa
- .msvp
- .musc
- .npsf
- .pnb
- .psh
- .rkv
- .rnd
- .rstm
- .rws
- .rxw
- .snd
- .sfs
- .sl3
- .smpl (w/ bad flags)
- .ster
- .str+.sth
- .str (MGAV blocked)
- .sts
- .svag
- .svs
- .tec (w/ bad flags)
- .tk5 (w/ bad flags)
- .vas
- .vag
- .vgs (w/ bad flags)
- .vig
- .vpk
- .vs
- .vsf
- .wp2
- .xa2
- .xa30

GC/Wii DSP ADPCM:
- .aaap
- .agsc
- .amts
- .asr
- .bns
- .bo2
- .capdsp
- .cfn
- .ddsp
- .dsp
  - standard, optional dual file stereo
  - RS03
  - Cstr
  - _lr.dsp
  - MPDS
- .gca
- .gcm
- .gsp+.gsp
- .hps
- .idsp
- .ish+.isd
- .lps
- .mpdsp
- .mss
- .mus (not quite right)
- .ndp
- .pdt
- .sdt
- .smp
- .sns
- .spt+.spd
- .ssm
- .stm/.dsp
- .str
- .str+.sth
- .sts
- .swd
- .thp, .dsp
- .tydsp
- .vjdsp
- .waa, .wac, .wad, .wam
- .was
- .wsd
- .wsi
- .ydsp
- .ymf
- .zwdsp

PCM:
- .aiff (8 bit, 16 bit)
- .asd (16 bit)
- .baka (16 bit)
- .bh2pcm (16 bit)
- .dmsg (16 bit)
- .gcsw (16 bit)
- .gcw (16 bit)
- .his (8 bit)
- .int (16 bit)
- .pcm (8 bit, 16 bit)
- .kraw (16 bit)
- .raw (16 bit)
- .rwx (16 bit)
- .sap (16 bit)
- .snd (16 bit)
- .sps (16 bit)
- .str (16 bit)
- .xss (16 bit)
- .voi (16 bit)
- .wb (16 bit)
- .zsd (8 bit)

Xbox IMA ADPCM:
- .matx
- .wavm
- .wvs
- .xmu
- .xvas
- .xwav

Yamaha ADPCM:
- .adpcm
- .dcs+.dcsw
- .str
- .spsd

IMA ADPCM:
- .bar (IMA ADPCM)
- .dvi (DVI IMA ADPCM)
- .hwas (IMA ADPCM)
- .idvi (DVI IMA ADPCM)
- .ivaud (IMA ADPCM)
- .myspd (IMA ADPCM)
- .stma (DVI IMA ADPCM)
- .strm (IMA ADPCM)

multi:
- .aifc (SDX2 DPCM, DVI IMA ADPCM)
- .asf, .as4 (8/16 bit PCM, EACS IMA ADPCM)
- .ast (GC AFC ADPCM, 16 bit PCM)
- .aud (IMA ADPCM, WS DPCM)
- .aus (PSX ADPCM, Xbox IMA ADPCM)
- .brstm (GC DSP ADPCM, 8/16 bit PCM)
- .emff (PSX APDCM, GC DSP ADPCM)
- .fsb, .wii (PSX ADPCM, GC DSP ADPCM, Xbox IMA ADPCM)
- .genh (lots)
- .musx (PSX ADPCM, Xbox IMA ADPCM, DAT4 IMA ADPCM)
- .nwa (16 bit PCM, NWA DPCM)
- .psw (PSX ADPCM, GC DSP ADPCM)
- .rwar, .rwav (GC DSP ADPCM, 8/16 bit PCM)
- .rwsd (GC DSP ADPCM, 8/16 bit PCM)
- .rsd (PSX ADPCM, 16 bit PCM, GC DSP ADPCM, Xbox IMA ADPCM, Radical ADPCM)
- .rrds (NDS IMA ADPCM)
- .sad (GC DSP ADPCM, NDS IMA ADPCM, Procyon Studios NDS ADPCM)
- .seg (Xbox IMA ADPCM, PS2 ADPCM)
- .sng, .asf, .str, .eam (EA/XA ADPCM or PSX ADPCM)
- .strm (NDS IMA ADPCM, 8/16 bit PCM)
- .ss7 (EACS IMA ADPCM, IMA ADPCM)
- .swav (NDS IMA ADPCM, 8/16 bit PCM)
- .xwb (16 bit PCM, Xbox IMA ADPCM)
- .wav, .lwav (unsigned 8 bit PCM, 16 bit PCM, GC DSP ADPCM, MS IMA ADPCM)

etc:
- .2dx9 (MS ADPCM)
- .aax (CRI ADX ADPCM)
- .acm (InterPlay ACM)
- .adp (GC DTK ADPCM)
- .adx (CRI ADX ADPCM)
- .afc (GC AFC ADPCM)
- .ahx (MPEG-2 Layer II)
- .aix (CRI ADX ADPCM)
- .baf (Blur ADPCM)
- .bgw (FFXI PS-like ADPCM)
- .bnsf (G.722.1)
- .caf (Apple IMA4 ADPCM)
- .de2 (MS ADPCM)
- .kcey (EACS IMA ADPCM)
- .lsf (LSF ADPCM)
- .mwv (Level-5 0x555 ADPCM)
- .ogg, .logg (Ogg Vorbis)
- .p3d (Radical ADPCM)
- .rsf (CCITT G.721 ADPCM)
- .sab (Worms 4 soundpacks)
- .s14/.sss (G.722.1)
- .sc (Activision EXAKT SASSC DPCM)
- .scd (MS ADPCM, MPEG Audio, 16 bit PCM)
- .sd9 (MS ADPCM)
- .smp (MS ADPCM)
- .spw (FFXI PS-like ADPCM)
- .stm renamed .ps2stm (DVI IMA ADPCM)
- .str (SDX2 DPCM)
- .stx (GC AFC ADPCM)
- .um3 (Ogg Vorbis)
- .xa (CD-ROM XA audio)

loop assists:
- .mus (playlist for .acm)
- .pos (loop info for .wav)
- .sli (loop info for .ogg)
- .sfl (loop info for .ogg)

