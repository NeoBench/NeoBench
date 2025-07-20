use std::process::Command;
use std::path::Path;

fn main() {
    println!("NeoBench 0.1 boot sequence started.");
    
    let desktop = Path::new("/usr/bin/neobench-desktop");
    if desktop.exists() {
        println!("Launching NeoBench Desktop...");
        let _ = Command::new(desktop).spawn();
    } else {
        println!("Desktop not found. Falling back to NeoShell...");
        let _ = Command::new("/bin/neosh").spawn();
    }

    // Wait forever
    loop {
        std::thread::sleep(std::time::Duration::from_secs(1));
    }
}
