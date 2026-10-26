<p align="center"><img src="docs/img/acid.png" alt="KARMA on the FM-1: the ACID track's screen" width="240"></p>

<h1 align="center">KARMA</h1>

<p align="center"><b>Free, open-source firmware for the M-VAVE FM-1.</b><br>
Four tracks, a drum synth, a 64-step sequencer, generators that keep writing while you play, and a cast of pixel characters.</p>

<p align="center">
<a href="https://eatyourgreensstudio-dotcom.github.io/karma-fm1/"><b>Website</b></a> ·
<a href="https://eatyourgreensstudio-dotcom.github.io/karma-fm1/emu/"><b>Try it in the browser</b></a> ·
<a href="https://eatyourgreensstudio-dotcom.github.io/karma-fm1/webapp/editor/">Web editor</a> ·
<a href="../../issues">Report a bug</a>
</p>

---

> **Status:** 3.0, before hardware testing. The browser simulator runs the real firmware today; the installer opens
> once KARMA has been tested on a real FM-1. Never for sale, free forever.

## What's inside

- **Track 1: ACID or JUNO.** A TB-303 voice (from X0X) or a Juno-106 style polysynth (from FM-106).
- **Track 2: ELEMENTS.** Physical modelling built on Mutable Instruments' open-source Elements code: strike, pluck, bow or blow a modal resonator; the Ominous Voice.
- **Track 3: PLAITS.** 24 synthesis models built on Mutable Instruments' open-source Plaits code.
- **Drums: HEX.** Its own LXR-style drum synthesizer, no samples.
- **Sequencer.** 64 steps a track, chords of up to 4 notes, ratchets, accents, slides, chance, PLAY modes, a song arranger.
- **Generators.** GEN writes every synth track's part on one KEY and chord PROGRESSION (SEQ > HARMONY): an acid bass, a voice-led line, a lead motif, evolving as they play. ACID GEN (TB-3PO), DRUM GEN (Grids maps, Euclid).
- **Effects.** CLOUD granular send (built on Mutable Instruments' Clouds code), reverb, delay, punch-in effects.
- **USB.** Class-compliant stereo audio interface and MIDI; per-track stems are in progress.
- **Screens.** OP-1-inspired pixel art drawn for KARMA: every track has a character that reacts to the notes.

## Build

See [BUILDING.md](BUILDING.md): the firmware (`sh build.sh`, JieLi clang in Docker), the host tests
(`sh tests/run_tests.sh`), the browser simulator (`sh web/emu/build.sh`) and the website (`sh tools/make_pages.sh`, served from `docs/`).

## Credits and licence

KARMA is by Ariel Muchnik. It is a fork of [SLOOP](https://github.com/isod89/sloop-fm1) by isod89, based on
[Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita, Hügelton Instruments. It uses code from
[X0X](https://github.com/charlesvestal/fm1-x0x) (Charles Vestal), [FM-106](https://github.com/PatttF/fm-106) (PatttF),
Mutable Instruments' [Plaits, Elements, Clouds and Grids](https://github.com/pichenettes/eurorack) (Émilie Gillet) and
ideas and code from [JIANT FM](https://github.com/juanjiant-bit/JIANT) (Juan Jiant). Details: [LICENSING.md](LICENSING.md).

KARMA is free software under the GNU General Public License, version 3 only ([LICENSE](LICENSE)). If you share KARMA
or firmware made from it, share its complete source under the same licence.

KARMA is not affiliated with or endorsed by M-VAVE, Mutable Instruments, Korg, Teenage Engineering, Hügelton
Instruments or Juan Jiant. M-VAVE and FM-1 are trademarks of their owners. The README of SLOOP, which KARMA grew
from, is kept as [SLOOP-README.md](SLOOP-README.md).
