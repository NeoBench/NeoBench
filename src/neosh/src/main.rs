fn main() {
    println!("\nWelcome to NeoShell (c) 2025");
    println!("Type 'help' or 'exit' to quit.\n");

    use std::io::{self, Write};
    loop {
        print!("NeoShell> ");
        io::stdout().flush().unwrap();

        let mut input = String::new();
        if io::stdin().read_line(&mut input).is_ok() {
            let cmd = input.trim();
            if cmd == "exit" {
                break;
            } else if cmd == "help" {
                println!("Commands: help, exit");
            } else {
                println!("Unknown command: {cmd}");
            }
        }
    }
}
