#pragma once

typedef int (*NeoCommandFunc)(int argc, char** argv);

struct NeoCommand
{
    const char* name;
    NeoCommandFunc func;
};

void Console_Write(const char* text);
void Console_ReadLine(char* buffer, int max);

int Neo_Execute(char* line);
