#pragma once
// Built-in cinematic title fonts (SIL OFL 1.1). Use as "builtin:<name>".
struct CineFont { const char* name; const unsigned char* data; unsigned size; };
extern const CineFont g_cine_fonts[];
extern const int g_cine_font_count;
