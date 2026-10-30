#!/bin/sh
# setup.sh: fetches the upstream sources at the commits this port was made against and applies
# the port's patches. Run once from the repository's top folder; then build with
#   python3 tools/build3ds.py uw1      (or uw2)
set -e
UE_COMMIT=e5deadd6945b492636aadccc14f52caa908b4186     # abedegno/underworld-exhumed
EXHUME_COMMIT=a34316463c230bafa3d4f0aa3b894a574e5fc738 # abedegno/Exhume (its submodule)
NUKED_COMMIT=765ec962e473aeb767e4cba74ffdc8f588ffbfe8                                    # nukeykt/Nuked-OPL3 (LGPL-2.1, not committed here)
if [ ! -d upstream ]; then
  git clone https://github.com/abedegno/underworld-exhumed.git upstream
  git -C upstream checkout -q "$UE_COMMIT"
  git -C upstream submodule update --init exhume
  git -C upstream/exhume checkout -q "$EXHUME_COMMIT"
  git -C upstream apply ../patches/underworld-exhumed-3ds.patch
  git -C upstream/exhume apply ../../patches/exhume-3ds.patch
fi
if [ ! -d vendor/nuked-opl3 ]; then
  git clone https://github.com/nukeykt/Nuked-OPL3.git vendor/nuked-opl3
  git -C vendor/nuked-opl3 checkout -q "$NUKED_COMMIT"
fi
echo "ready: python3 tools/build3ds.py uw1"
