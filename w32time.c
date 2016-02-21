#include <windows.h>
#include <tchar.h>
#include <stdint.h>
#include <stdio.h>
#include <shlwapi.h>

#define TCHARSIZE sizeof(TCHAR)
#define PATHSEP _T(';')

typedef union {
   FILETIME FileTime;
   LARGE_INTEGER LargeInteger;
   uint64_t uint64;
} TimeUnion;

/* Allocate a buffer for a string of 'n' characters */
static LPTSTR AllocString(size_t n)
{
   return (LPTSTR)LocalAlloc(LMEM_FIXED, TCHARSIZE * n);
}

/* Return a pointer to the next string after the next \0-char */
static LPCTSTR NextString(LPCTSTR str)
{
   while (*str)
      str++;
   str++;
   return str;
}

/* Formats a message (allocates buffer) */
static LPTSTR SPrintF(LPTSTR pMessage, ...)
{
   LPTSTR pBuffer = NULL;
   va_list args = NULL;
   va_start(args, pMessage);
   FormatMessage(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER,
      pMessage, 0, 0, (LPTSTR)&pBuffer, 0, &args);
   va_end(args);
   return pBuffer;
}

/* Formats a message using va_list (allocates buffer) */
static LPTSTR SPrintFV(LPCTSTR message, va_list args)
{
   LPTSTR pBuffer = NULL;
   FormatMessage(FORMAT_MESSAGE_FROM_STRING |
      FORMAT_MESSAGE_ALLOCATE_BUFFER,
      message,
      0,
      0,
      (LPTSTR)&pBuffer,
      0,
      &args);
   return pBuffer;
}

/* Prints a message to STDOUT */
static void Print(LPCTSTR pMessage)
{
   DWORD written;
   WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), pMessage, lstrlen(pMessage), &written, NULL);
}

/* Prints a formatted message to STDOUT, using va_list */
static void PrintFV(LPCTSTR message, va_list args)
{
   LPTSTR formattedMessage = SPrintFV(message, args);
   Print(formattedMessage);
   LocalFree(formattedMessage);
}

/* Prints a formatted message to STDOUT */
static void PrintF(LPCTSTR message, ...)
{
   va_list args = NULL;
   va_start(args, message);
   PrintFV(message, args);
   va_end(args);
}

/* Gets the error message for a specified error code (allocates buffer) */
static LPTSTR GetErrorText(DWORD dwError)
{
   LPTSTR pBuffer = NULL;
   FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwError, LANG_NEUTRAL, (LPTSTR)&pBuffer, 0, NULL);
   return pBuffer;
}

/* Console control event handler */
static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
   switch (dwCtrlType) {
   case CTRL_C_EVENT:
   case CTRL_BREAK_EVENT:
      return TRUE;
   case CTRL_CLOSE_EVENT:
   case CTRL_LOGOFF_EVENT:
   case CTRL_SHUTDOWN_EVENT:
   default:
      return FALSE;
   }
}

/* Prints error and error message (from GetLastError) and exits */
static void AbortLastError(LPCTSTR message)
{
   LPTSTR errorText = GetErrorText(GetLastError());
   PrintF(_T("%1: %2"), message, errorText);
   LocalFree(errorText);
   ExitProcess(1);
}

/* Prints error and exits */
static void Abort(LPCTSTR message)
{
   PrintF(_T("%1\n"), message);
   ExitProcess(1);
}

/* Generate a \0-separated list of element in the PATHEXT environment variable */
static LPCTSTR GeneratePathExt()
{
   static TCHAR pathext[MAX_PATH];
   GetEnvironmentVariable(_T("PATHEXT"), pathext, MAX_PATH);
   LPTSTR p = pathext;
   while (*p) {
      if (*p == PATHSEP)
         *p = 0;
      p++;
   }
   p[1] = 0;
   return pathext;
}

/* Get the PATHEXT environment variable as \0-separated list */
static LPCTSTR GetPathExt()
{
   static LPCTSTR pathExt = NULL;
   if (pathExt == NULL)
      pathExt = GeneratePathExt();
   return pathExt;
}

/* Find a file in PATH trying each possible extension in PATHEXT */
static BOOL SearchPathWithPathExt(LPCTSTR Program, LPTSTR _Out_ ProgramPath)
{
   LPCTSTR pathext = GetPathExt();
   if (!pathext)
      return FALSE;
   while (*pathext) {
      TCHAR candidate[MAX_PATH];
      lstrcpy(candidate, Program);
      PathAddExtension(candidate, pathext);
      DWORD Result = SearchPath(NULL, candidate, NULL, MAX_PATH, ProgramPath, NULL);
      if (Result != 0)
         return TRUE;
      pathext = NextString(pathext);
   }
   return FALSE;
}

/* Find a file in PATH using PATHEXT if necessary */
static BOOL SearchPathAllowPathExt(LPCTSTR Program, LPTSTR _Out_ ProgramPath)
{
   DWORD Result = SearchPath(NULL, Program, NULL, MAX_PATH, ProgramPath, NULL);
   if (Result == 0)
      return SearchPathWithPathExt(Program, ProgramPath);
   else
      return TRUE;
}

void ExecReportTimes(LPCTSTR ProgramPath, LPTSTR CommandLine)
{
   TimeUnion CreationTime;
   TimeUnion ExitTime;
   TimeUnion KernelTime;
   TimeUnion UserTime;
   STARTUPINFO StartupInfo;
   PROCESS_INFORMATION ProcessInformation;

   SecureZeroMemory(&StartupInfo, sizeof(StartupInfo));
   StartupInfo.cb = sizeof(StartupInfo);

   if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
      AbortLastError(_T("SetConsoleCtrlHandler"));
   if (!CreateProcess(ProgramPath, CommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &StartupInfo, &ProcessInformation))
      AbortLastError(_T("CreateProcess"));
   WaitForSingleObject(ProcessInformation.hProcess, INFINITE);
   if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, FALSE))
      AbortLastError(_T("SetConsoleCtrlHandler"));

   if (!GetProcessTimes(ProcessInformation.hProcess, &CreationTime.FileTime, &ExitTime.FileTime, &KernelTime.FileTime, &UserTime.FileTime))
      AbortLastError(_T("GetProcessTimes"));

   uint64_t real = (ExitTime.uint64 - CreationTime.uint64) / 10000;
   uint64_t system = KernelTime.uint64 / 10000;
   uint64_t user = UserTime.uint64 / 10000;

   fprintf(stderr, "real    %.3f\n", real / 1000.0);
   fprintf(stderr, "system  %.3f\n", system / 1000.0);
   fprintf(stderr, "user    %.3f\n", user / 1000.0);

   DWORD dwExitCode;
   if (!GetExitCodeProcess(ProcessInformation.hProcess, &dwExitCode))
      AbortLastError(_T("GetExitCodeProcess"));
   CloseHandle(ProcessInformation.hProcess);
   CloseHandle(ProcessInformation.hThread);
   ExitProcess(dwExitCode);
}

int main()
{
   LPTSTR CommandLine = GetCommandLine();

   LPTSTR ChildCommandLine = PathGetArgs(CommandLine);
   if (*ChildCommandLine == '\0')
   {
      fprintf(stderr, "Usage: w32time <command>\n");
      return -1;
   }

   LPTSTR ChildArguments = PathGetArgs(ChildCommandLine);
   LPTSTR CommandEnd = ChildCommandLine;
   DWORD CommandLength = ChildArguments - ChildCommandLine;
   while (CommandLength > 0 && ChildCommandLine[CommandLength-1] == _T(' '))
      --CommandLength;

   LPTSTR ChildCommand = AllocString(CommandEnd - ChildCommandLine + 1);
   lstrcpyn(ChildCommand, ChildCommandLine, CommandLength + 1);
   TCHAR FinalApplicationName[MAX_PATH];
   SearchPathAllowPathExt(ChildCommand, FinalApplicationName);

   ExecReportTimes(FinalApplicationName, ChildCommandLine);

   return 0;
}
