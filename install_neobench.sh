; install_neobench.sh — Amiga install script
; Usage: execute install_neobench.sh DH0:

.set TARGET $1

echo "== Creating NeoBench directory structure =="
makedir $TARGET:C
makedir $TARGET:L
makedir $TARGET:Libs
makedir $TARGET:Devs
makedir $TARGET:Fonts
makedir $TARGET:Prefs
makedir $TARGET:S
makedir $TARGET:Programs
makedir $TARGET:Internet
makedir $TARGET:Temp
makedir $TARGET:NeoBench
makedir $TARGET:NeoBench/Backdrops
makedir $TARGET:NeoBench/Icons

echo "== Copying system files and drivers =="
copy PFS3_AIO L: TO $TARGET:L
copy mountlist TO $TARGET:Devs/
copy HDToolBox TO $TARGET:Programs/
copy NeoBenchPrefs TO $TARGET:Prefs/
copy crossdos.device TO $TARGET:Devs/
copy usbdisk.device TO $TARGET:Devs/
copy keymap.library TO $TARGET:Libs/
copy icon.library TO $TARGET:Libs/
copy neobench.info TO $TARGET:NeoBench/Icons/
copy backdrop1.iff TO $TARGET:NeoBench/Backdrops/
copy backdrop2.iff TO $TARGET:NeoBench/Backdrops/
copy splash.iff TO $TARGET:NeoBench/Backdrops/

echo "== Copying NeoBench Apps =="
copy NeoBenchInfo TO $TARGET:Programs/
copy NeoBenchCalc TO $TARGET:Programs/
copy NeoBenchCalendar TO $TARGET:Programs/
copy NeoBenchHDToolBox TO $TARGET:Programs/
copy NeoBenchShell TO $TARGET:C/
copy NeoBenchUpdate TO $TARGET:Internet/
copy NeoBenchBluetooth TO $TARGET:Devs/
copy NeoBenchUSB TO $TARGET:Devs/

echo "== Copying modern datatypes =="
copy png.datatype TO $TARGET:Libs/datatype/
copy modernsound.datatype TO $TARGET:Libs/datatype/

echo "== Setting up PFS3 as filesystem =="
copy PFS3_AIO TO $TARGET:L/PFS3_AIO

echo "== Generating startup-sequence =="
echo "NeoBench booting..." > $TARGET:S/startup-sequence
echo "SetPatch QUIET" >> $TARGET:S/startup-sequence
echo "Assign NeoBench: $TARGET:NeoBench" >> $TARGET:S/startup-sequence
echo "cd NeoBench:" >> $TARGET:S/startup-sequence
echo "run >nil: C/NeoShell" >> $TARGET:S/startup-sequence
echo "run >nil: NeoBench/NeoBenchBar" >> $TARGET:S/startup-sequence

echo "echo NeoBench Boot Complete." > $TARGET:S/user-startup

echo "NeoBench installation finished!"
