[English](README.md) · [עברית](README.he.md)

<p align="center"><img src="assets/heben-logo.png" width="116" alt="לוגו המיקרופון של HebEN"></p>
<h1 align="center">HebEN Dictation</h1>

<p align="center"><strong>הכתבה מקומית בעברית ובאנגלית ל־Windows.</strong><br>לוחצים על קיצור מקשים, מדברים, והטקסט המפוענח מוזן ישירות לשדה העריכה הפעיל.</p>

<p align="center">
<a href="https://github.com/NRG-Wardog/STT/releases/tag/v1.0.0">הורדת v1.0.0</a> ·
<a href="docs/DEVELOPMENT.md">מדריך פיתוח מלא</a> ·
<a href="docs/BENCHMARKS.md">מדידות וביצועים</a>
</p>

**מצב הפרויקט: v1.0.0 MVP.** מערכת ההכתבה המרכזית זמינה לשימוש. דיוק הזיהוי, תאימות ליישומים והכתבה הדרגתית מהירה הם תחומים שממשיכים להשתפר. **חבילת CPU הציבורית שונה** מהגרסאות הניסיוניות עם CUDA והמודל העברי המותאם.

![חלון הגדרת קיצור מקשים](docs/assets/shortcut-settings.png)

## יכולות עיקריות

- **זיהוי דיבור מקומי:** Whisper רב־לשוני בשילוב Silero VAD, ללא צורך בשירות תמלול בענן בזמן הריצה.
- **עברית ואנגלית:** אפשר לבחור שפה ידנית או בזיהוי אוטומטי. משפטים שמשלבים שפות עשויים להיות פחות מדויקים.
- **הקלדה ישירה:** הזרקת Unicode לשדה הממוקד ב־Windows. לוח ההעתקה אינו בשימוש כברירת מחדל.
- **יישום Windows מקורי:** ‏C++/Win32, סמל מיקרופון במגש המערכת, קיצור מקשים גלובלי הניתן לשינוי ונקודה ליד הסמן כשניתן לזהות את מיקומו.
- **Streaming ניסיוני:** אפשרות להצגת מילים בהדרגה, **כבויה כברירת מחדל**. במצב הרגיל הטקסט נשלח לאחר מקטע דיבור או הפסקה.

## הורדה והתקנה — ללא קומפיילר

1. הורידו את [HebEN-Dictation-v1.0.0-Windows-CPU-x64.zip](https://github.com/NRG-Wardog/STT/releases/download/v1.0.0/HebEN-Dictation-v1.0.0-Windows-CPU-x64.zip) מדף [Releases](https://github.com/NRG-Wardog/STT/releases/tag/v1.0.0).
2. חלצו את **כל תוכן הארכיון** לתיקייה קבועה עם הרשאת כתיבה, לדוגמה `C:\Apps\HebEN`. אין להריץ את ה־EXE מתוך ZIP ואין להפריד אותו מ־`config.ini` ומתיקיית `models\`.
3. ב־Windows היכנסו ל־**Settings → Privacy & security → Microphone** ואפשרו גישה למיקרופון גם ליישומי שולחן עבודה. ודאו שהמיקרופון הנכון נבחר.
4. הפעילו `HebENDictation.exe`. הסמל מופיע ב־**System Tray**, לעיתים מאחורי כפתור הסמלים המוסתרים (`^`).
5. מקדו שדה טקסט (מומלץ להתחיל ב־Notepad). לחצו ושחררו **Ctrl+Shift+R**, דברו, ולחצו שוב כדי לעצור. בהתאם למנגנון זיהוי השקט, גם הפסקת דיבור עשויה לסיים מקטע. השאירו את שדה הטקסט במיקוד עד שהטקסט נכתב.

החבילה מכילה EXE ל־CPU, מודל Whisper multilingual small Q5_1, מודל Silero VAD, הגדרות ורישיונות. **אין צורך ב־Python או בחיבור לאינטרנט לצורך הכתבה רגילה לאחר ההתקנה.**

אפשר לאמת את קובץ ההורדה מול ה־SHA-256 בדף הגרסה:

```powershell
Get-FileHash .\HebEN-Dictation-v1.0.0-Windows-CPU-x64.zip -Algorithm SHA256
```

### דרישות מערכת

| רכיב | דרישה |
|---|---|
| מערכת הפעלה | Windows 10/11 בגרסת x64 |
| מעבד | x64 עם AVX2, ‏FMA, ‏F16C ו־BMI2 עבור חבילת CPU הרשמית |
| שמע | מיקרופון והרשאת גישה ליישומי Desktop |
| זיכרון ואחסון | בהתאם למודל; יש לפנות מקום לקובצי המודלים ולזיכרון בזמן הסקה |
| רשת | נדרשת להורדות, אך לא לתמלול מקומי אחרי ההתקנה |
| GPU אופציונלי | NVIDIA תואם, דרייבר ו־CUDA Toolkit; **אינו כלול ב־v1.0.0 CPU** |

## שימוש והגדרות

לחצו לחיצה ימנית על סמל המיקרופון במגש המערכת. דרך התפריט אפשר להגדיר **Keyboard shortcut…**, לבחור **Language** ולטעון מחדש הגדרות באמצעות **Reload settings**. קיצור ברירת המחדל הוא `Ctrl+Shift+R`. קיצור שהצליח להירשם ב־Windows עדיין יכול להתנגש בקיצור מקומי של תוכנה אחרת. מערכת הקיצור הגלובלי אינה מבדילה בין Ctrl ימני לשמאלי; לכן גם `Right Ctrl+?` אינו מוגבל למקש הימני בלבד.

- **הפעלה עם Windows:** העבירו קודם את התיקייה למיקום קבוע, לחצו `Win+R`, כתבו `shell:startup`, וצרו שם קיצור דרך ל־`HebENDictation.exe`. החבילה הניידת אינה עושה זאת בעצמה.
- **בחירת שפה:** אוטומטי, עברית או אנגלית. אם משפטים קצרים בעברית מזוהים כשפה אחרת, בחרו עברית ידנית.
- **הכתבה הדרגתית:** הגדירו `streaming_prototype=1` ב־`config.ini` ובחרו **Reload settings**. זוהי אפשרות ניסיונית: המנגנון לא מתקן אוטומטית מילים שכבר נכתבו כאשר המודל משנה את ההשערה.
- **מסופים (Terminals):** ‏`trusted_terminal_input=1` הוא אישור מפורש לקלט בהנחיות מוכרות ולא רגישות. המערכת לא תמיד יודעת לזהות בקשת סיסמה במסוף. HebEN אינו שולח Enter אוטומטית.
- **Clipboard:** הזרקת Unicode היא ברירת המחדל. מנגנון הדבקה דרך הלוח ניסיוני ואינו מומלץ כרגע, כיוון שבדיקות שימור הלוח נכשלו בעבר.

## קומפילציה מקוד מקור — Windows

סביבת הפיתוח היא **Visual Studio 2022 עם CMake**. הדרך המומלצת היא `build.ps1`: הגדרת CMake, קומפילציה, בדיקות יחידה, הורדת מודלים ואימות SHA-256, ואז יצירת תיקיית `dist\` מוכנה להרצה.

### שלב 1 — התקנת כלי פיתוח

נדרשים:

- **Visual Studio 2022** או Build Tools, עם התוסף **Desktop development with C++**, כלי MSVC ל־x64, ‏Windows SDK וכלי CMake.
- **Git for Windows**.
- **CMake 3.21 ומעלה** שנגיש מתוך `PATH` (אפשר להשתמש בגרסה שמגיעה עם Visual Studio).
- **PowerShell**. ‏Python נחוץ רק לחלק מסקריפטי המחקר והבדיקות הנוספים, ולא לקומפילציה הרגילה.

בדקו שהכלים זמינים:

```powershell
git --version
cmake --version
```

### שלב 2 — הורדת הקוד ובנייה אוטומטית ל־CPU

פתחו PowerShell או Developer PowerShell של Visual Studio:

```powershell
git clone https://github.com/NRG-Wardog/STT.git
cd STT
.\build.ps1 -Model small
```

אם מדיניות הרצת הסקריפטים של PowerShell חוסמת את הקובץ, בדקו את תוכנו, ואם אתם סומכים עליו הריצו עם עקיפה **לתהליך הנוכחי בלבד**:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Model small
```

בסיום תקין תתקבל תיקייה עם הקבצים הדרושים:

```text
dist/
  HebENDictation.exe
  config.ini
  models/
    ggml-small-q5_1.bin
    ggml-silero-v6.2.0.bin
  קובצי רישיונות והוראות קצרות
```

כעת מריצים:

```powershell
.\dist\HebENDictation.exe
```

**שימו לב:** `build.ps1` מעדכן את `dist\HebENDictation.exe` ועלול לשנות את ערך המודל ב־`dist\config.ini`. אם התוכנה כבר רצה מתוך אותה תיקייה, יש לצאת ממנה לפני הבנייה, או לעבוד בעותק פיתוח נפרד. אין להריץ את הסקריפט על התקנה שחשוב לשמר ללא גיבוי.

האפשרויות `-Model base` ו־`-Model tiny` משתמשות במודלים רב־לשוניים קטנים יותר, שעלולים לפגוע בדיוק בעברית. ‏`-SkipModelDownload` מתאים רק כאשר קובצי המודלים כבר קיימים ונבדקו ידנית; הוא לא מיועד להתקנה הראשונה.

### שלב 3 — קומפילציה ידנית באמצעות CMake

מי שרוצה שליטה באפשרויות הקומפילציה יכול לבנות לתיקיית פיתוח נפרדת:

```powershell
cmake -S . -B build-dev -G "Visual Studio 17 2022" -A x64 -DGGML_CUDA=OFF
cmake --build build-dev --config Release --parallel 4 --target HebENDictation HebENDetectorTest HebENUnicodeInputTest HebENClipboardMockTest HebENStablePrefixTest HebENHotkeyPolicyTest HebENHotkeyRegistryTest
ctest --test-dir build-dev -C Release --output-on-failure
```

התוצאה היא `build-dev\Release\HebENDictation.exe`. **זה עדיין אינו Package מלא**: ההפעלה דורשת גם `config.ini` ואת שני קובצי המודלים בנתיבים המתאימים. אפשר להשתמש ב־`build.ps1` ליצירת `dist\`, או לפעול לפי [מדריך ההרכבה הידנית](docs/DEVELOPMENT.md#manual-build-and-runtime-staging), הכולל הורדות מודלים ובדיקות SHA-256.

### שלב 4 — בניית CUDA אופציונלית

בניית GPU דורשת NVIDIA מתאים, דרייבר, CUDA Toolkit וספריות Runtime נוספות. שינוי `backend=cpu` ל־`backend=cuda` בקובץ ההגדרות של EXE שמקומפל ל־CPU **לא הופך אותו לגרסת GPU**.

```powershell
cmake -S . -B build-cuda -G "Visual Studio 17 2022" -A x64 -DGGML_CUDA=ON
cmake --build build-cuda --config Release --parallel 4 --target HebENDictation
```

צריך להכין עבור CUDA תיקיית הרצה נפרדת, מודלים מתאימים, `backend=cuda` וספריות התלויות בגרסה. **אין חבילת CUDA מוכנה ב־Release v1.0.0.** פרטים ב־[מדריך CUDA](docs/DEVELOPMENT.md#cuda-development-build).

### שלב 5 — בדיקות ויצירת חבילת הפצה

```powershell
ctest --test-dir build-dev -C Release --output-on-failure
powershell.exe -NoProfile -File .\tests\check-icon-resource.ps1 -Exe .\build-dev\Release\HebENDictation.exe
python .\tests\test-rootcause-errors.py   # אופציונלי; דורש Python
```

שש בדיקות ה־CTest הן בדיקות יחידה דטרמיניסטיות. **הן אינן בודקות הכתבה פיזית מהמיקרופון או כתיבה אמיתית לתוך ChatGPT.** להפצת Release דרושים גם מודלים מאומתים, קובצי רישיונות, מזהה Commit וקובצי Checksum; השתמשו ב־[הוראות אריזת Release](docs/DEVELOPMENT.md#release-packaging) ולא רק ב־ZIP של תיקיית קומפילציה.

## פתרון תקלות

| תקלה | מה לבדוק |
|---|---|
| הפקודה `cmake` או `git` לא מזוהה | התקינו את התוכנה הרלוונטית, פתחו PowerShell מחדש ובדקו `PATH`. |
| CMake לא מוצא קומפיילר או דוחה `-A x64` | ודאו שמותקנים MSVC ו־Windows SDK. השתמשו ב־Visual Studio Generator, ופתחו **תיקיית Build חדשה** אם משנים Generator. |
| הורדת המודל או בדיקת SHA-256 נכשלה | בדקו קישוריות ואת קובץ המקור; אל תבטלו את בדיקות השלמות כדי לעקוף שגיאה. |
| אי אפשר להחליף את ה־EXE ב־`dist\` | צאו מהיישום הפעיל דרך ה־Tray; ייתכן ש־Windows נועל את הקובץ. |
| התוכנה נפתחת אך לא מתמללת | בדקו מיקום `config.ini`, את שני קובצי המודל, את המיקרופון ואת ההרשאות. |
| האייקון חסר או הקיצור לא עובד | בדקו את הסמלים המוסתרים, התנגשות קיצורים ועותק HebEN אחר שכבר רץ. |
| הטקסט לא מופיע במקום הרצוי | השאירו את שדה הקלט במיקוד; בדקו קודם ב־Notepad. יש עורכים ומסופים שחוסמים קלט מוזרק. |
| עברית מזוהה כשפה אחרת | בחרו עברית ידנית והשתמשו במודל רב־לשוני, לא במודל אנגלי מסוג `*.en.bin`. |

## תאימות, ביצועים ומגבלות

נבדקו מסלולי הקלדה ב־Notepad, ‏Chrome/ChatGPT, ‏VS Code ומסוף Codex בבדיקות קצרות ומבוקרות על **גרסאות ניסוי שונות**. זה אינו מבטיח תאימות לכל שדה או לכול יישום.

| מדידה היסטורית | תוצאה | הקשר |
|---|---:|---|
| זיכרון פרטי / Working Set בסרק | 4.00 / 13.56 MiB חציון | מועמד CUDA ‏`c5ff8e…`, ‏59 דגימות |
| מיקרופון פתוח ללא דיבור מזוהה | 1,118 / 158.9 MiB חציון | אותו מועמד CUDA, ללא הסקת ASR |
| הופעת טקסט חלקי ראשון | 3.28 שניות חציון | Fixture אחר `ed7e43…`; אותה הקלטה חמש פעמים |
| WER עברית / אנגלית / משולב מלאכותי | 20.0% / 7.2% / 28.1% | ניסוי היברידי אחר `517ca1…`; ‏30 הקלטות בכל קבוצה, תמלילי ייחוס שלא אומתו עצמאית |

**אלה מדידות של גרסאות ניסוי שונות — לא מדדי חבילת CPU הרשמית ולא מדידה מייצגת של הדיבור האישי של המשתמש.** ראו [שיטות המדידה](docs/BENCHMARKS.md).

מגבלות ידועות: החלפת שפות ומשפטים קצרים עלולים להתמלל לא נכון; מיקום הסמן אינו חשוף בכל יישום; שינויים בפוקוס, חלונות מוגבהים ומסופים עשויים למנוע הזרקה; Streaming עשוי להכניס טקסט מוקדם שהמודל יתקן בהשערה מאוחרת. מומלץ לבדוק טקסט חשוב לאחר הכתבה.

## תרומה לפרויקט ורישיונות

- הקוד מופץ ברישיון [MIT](LICENSE). למודלים, Whisper, ‏miniaudio, ‏Silero ורכיבי CUDA אופציונליים יש תנאים עצמאיים: [פירוט רישיונות](docs/LICENSING.md).
- [מדריך הפיתוח המלא](docs/DEVELOPMENT.md) כולל את הוראות הבנייה, הורדות המודלים, בדיקות והפצה.
- לפתיחת תקלה: [GitHub Issues](https://github.com/NRG-Wardog/STT/issues). צרפו גרסת Windows, חומרה, מודל ו־Backend, מזהה הבינארי ושלבי שחזור, **בלי מידע רגיש**.
- [הערות Release v1.0.0](docs/RELEASE-NOTES.md) מפרטות את הבדיקות שבוצעו ואת המגבלות.

הפרויקט נעזר ב־[whisper.cpp](https://github.com/ggml-org/whisper.cpp), ‏[OpenAI Whisper](https://github.com/openai/whisper), ‏[miniaudio](https://github.com/mackron/miniaudio), ‏[Silero VAD](https://github.com/snakers4/silero-vad) ובמודלים אופציונליים של [ivrit.ai](https://huggingface.co/ivrit-ai).
