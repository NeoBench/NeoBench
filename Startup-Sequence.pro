FailAt 21
echo "===================================================="
echo "||                                                ||"
echo "||   N E O B E N C H   S Y S T E M   L O A D E R   ||"
echo "||                                                ||"
echo "||   (c) LORD PROTECTOR 2026 - ARCH: m68k-native   ||"
echo "||                                                ||"
echo "===================================================="
echo ""
echo "Select Operating Environment:"
echo "1. NeoBench PRO (Linux-Enhanced Services)"
echo "2. NeoBench Desktop (Bare Metal Classic)"
echo ""
echo "Booting Option 1 in 5 seconds (Press 2 for Bare Metal)..."
echo ""

GetKeyPress
if $RC EQ 50
    echo " >>> INITIALIZING BARE METAL DESKTOP..."
    NeoLauncher 0
else
    echo " >>> INITIALIZING NEOBENCH PRO KERNEL..."
    amiboot -d -k vmlinux -r miniroot.img root=/dev/ram video=amifb:pal console=tty0
endif
