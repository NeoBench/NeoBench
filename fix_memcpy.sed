s/std::memcpy(\([^,]*\), *"[^"]*");/std::memcpy(\1, "\0", sizeof("\0")-1);/g
