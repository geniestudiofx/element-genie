// Element Genie - built-in 3D icon pack (original artwork, free to use)
#pragma once
struct IconDef { const char* name; const char* svg; };
static const IconDef ICONS[] = {
    {"Star", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M50.0 6.0 L61.4 36.4 L93.7 37.8 L68.4 58.0 L77.0 89.2 L50.0 71.3 L23.0 89.2 L31.6 58.0 L6.3 37.8 L38.6 36.4 Z"/></svg>)SVG"},
    {"Heart", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M50 90 C20 68 4 50 4 31 C4 16 15 6 28 6 C38 6 46 12 50 21 C54 12 62 6 72 6 C85 6 96 16 96 31 C96 50 80 68 50 90 Z"/></svg>)SVG"},
    {"Sparkle", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M50 2 C54 38 62 46 98 50 C62 54 54 62 50 98 C46 62 38 54 2 50 C38 46 46 38 50 2 Z"/></svg>)SVG"},
    {"Lightning", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M60 2 L16 56 L46 56 L36 98 L84 40 L54 40 L68 2 Z"/></svg>)SVG"},
    {"Crown", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M8 78 L13 26 L34 48 L50 12 L66 48 L87 26 L92 78 Z"/><path d="M8 83 L92 83 L92 94 L8 94 Z"/></svg>)SVG"},
    {"Ring", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path fill-rule="evenodd" d="M50 4 A46 46 0 1 0 50 96 A46 46 0 1 0 50 4 Z M50 24 A26 26 0 1 1 50 76 A26 26 0 1 1 50 24 Z"/></svg>)SVG"},
    {"Shield", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M50 4 L90 17 L90 47 C90 71 72 87 50 96 C28 87 10 71 10 47 L10 17 Z"/></svg>)SVG"},
    {"Arrow", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M4 38 L58 38 L58 16 L96 50 L58 84 L58 62 L4 62 Z"/></svg>)SVG"},
    {"Play", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path fill-rule="evenodd" d="M50 4 A46 46 0 1 0 50 96 A46 46 0 1 0 50 4 Z M40 29 L73 50 L40 71 Z"/></svg>)SVG"},
    {"Music note", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><ellipse cx="30" cy="78" rx="17" ry="13"/><path d="M38 76 L38 12 L84 4 L84 22 L47 29 L47 76 Z"/><ellipse cx="75" cy="68" rx="15" ry="11"/><path d="M82 66 L82 20 L90 18 L90 66 Z"/></svg>)SVG"},
    {"Mic", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M38 10 Q50 0 62 10 L62 50 Q50 60 38 50 Z"/><path d="M26 42 L33 42 Q33 64 50 64 Q67 64 67 42 L74 42 Q74 70 54 72 L54 84 L68 84 L68 92 L32 92 L32 84 L46 84 L46 72 Q26 70 26 42 Z"/></svg>)SVG"},
    {"Ticket", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M4 22 L96 22 L96 40 A10 10 0 0 0 96 60 L96 78 L4 78 L4 60 A10 10 0 0 0 4 40 Z"/></svg>)SVG"},
    {"Pin", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path fill-rule="evenodd" d="M50 97 C34 73 16 57 16 37 A34 34 0 0 1 84 37 C84 57 66 73 50 97 Z M50 22 A14 14 0 1 0 50 50 A14 14 0 1 0 50 22 Z"/></svg>)SVG"},
    {"Flame", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M50 97 C24 97 11 79 14 58 C17 40 31 31 32 12 C45 23 49 36 46 49 C55 43 59 32 58 21 C75 36 89 55 86 71 C84 87 70 97 50 97 Z"/></svg>)SVG"},
    {"Hexagon", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M50.0 4.0 L89.8 27.0 L89.8 73.0 L50.0 96.0 L10.2 73.0 L10.2 27.0 Z"/></svg>)SVG"},
    {"Check", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M6 52 L21 37 L40 56 L80 15 L95 30 L40 85 Z"/></svg>)SVG"},
    {"Speech", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M14 10 L86 10 Q96 10 96 20 L96 60 Q96 70 86 70 L46 70 L24 92 L28 70 L14 70 Q4 70 4 60 L4 20 Q4 10 14 10 Z"/></svg>)SVG"},
    {"Moon", R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="100" height="100" fill="#d8a640"><path d="M64 6 A46 46 0 1 0 94 70 A36 36 0 1 1 64 6 Z"/></svg>)SVG"},
};
static const int ICON_COUNT = sizeof(ICONS) / sizeof(ICONS[0]);
