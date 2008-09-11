#include <windows.h>
#include <stdint.h>
#include <stdio.h>

typedef union {
   FILETIME FileTime;
   LARGE_INTEGER LargeInteger;
   uint64_t uint64;
} TimeUnion;

LPTSTR SkipCommandLineWord(LPTSTR str)
{
   if (*str == '"') {
      str++;
      while (*str && *str != '"')
         str++;
      if (*str == '"')
         str++;
   } else {
      while (*str && *str != ' ')
         str++;
   }
   return str;
}

char *NextPath(char *s)
{
   while (*s && *s != ';')
      s++;
   while (*s == ';')
      s++;
   return s;
}

int main(int argc, char** argv)
{
   TimeUnion CreationTime;
   TimeUnion ExitTime;
   TimeUnion KernelTime;
   TimeUnion UserTime;
   BOOL r;
   STARTUPINFO StartupInfo;
   PROCESS_INFORMATION ProcessInformation;
   char FinalApplicationName[MAX_PATH];
   LPTSTR FilePart;
   char PathExt[MAX_PATH];

   LPTSTR CommandLine = GetCommandLine();

   LPTSTR ChildCommandLine = SkipCommandLineWord(CommandLine);
   while (*ChildCommandLine == ' ')
      ChildCommandLine++;

   if (*ChildCommandLine == '\0')
   {
      fprintf(stderr, "Usage: w32time <command>\n");
      return -1;
   }

   LPTSTR PastApplicationName = SkipCommandLineWord(ChildCommandLine);

   DWORD ApplicationNameLength = PastApplicationName - ChildCommandLine;
   LPTSTR ApplicationName = LocalAlloc(LMEM_FIXED, ApplicationNameLength + 1);
   CopyMemory(ApplicationName, ChildCommandLine, ApplicationNameLength);
   if (*ApplicationName == '"')
   {
      ApplicationName++;
      ApplicationNameLength -= 2;
   }
   ApplicationName[ApplicationNameLength] = '\0';


   GetEnvironmentVariable("PATHEXT", PathExt, sizeof(PathExt));
   DWORD PathLength;
   char *s, *n;
   char ext[16];
   BOOL found = FALSE;
   for (s = PathExt, n = NextPath(s); *s; s = n, n = NextPath(s))
   {
      CopyMemory(ext, s, n - s);
      ext[n-s] = '\0';
      if (ext[n-s-1] == ';')
         ext[n-s-1] = '\0';
      PathLength = SearchPath(NULL, ApplicationName, ext, MAX_PATH, FinalApplicationName, &FilePart);
      if (PathLength) {
         found = TRUE;
         break;
      }
   }
   if (!found)
   {
      fprintf(stderr, "`%s' not found.\n", ApplicationName);
      return -1;
   }

#if _DEBUG
   printf("CommandLine=\"%s\";\n", CommandLine);
   printf("ChildCommandLine=\"%s\";\n", ChildCommandLine);
   printf("ApplicationName=\"%s\";\n", ApplicationName);
   printf("FinalApplicationName=\"%s\";\n", FinalApplicationName);
#endif
   
   ZeroMemory(&StartupInfo, sizeof(StartupInfo));
   StartupInfo.cb = sizeof(StartupInfo);

   r = CreateProcess(FinalApplicationName, ChildCommandLine, NULL, NULL, TRUE, 0, NULL, NULL, &StartupInfo, &ProcessInformation);
   if (!r)
   {
      fprintf(stderr, "CreateProcess failed: %u\n", GetLastError());
      return -1;
   }

   WaitForSingleObject(ProcessInformation.hProcess, INFINITE);

   r = GetProcessTimes(ProcessInformation.hProcess, &CreationTime.FileTime, &ExitTime.FileTime, &KernelTime.FileTime, &UserTime.FileTime);
   if (!r)
   {
      fprintf(stderr, "GetProcessTimes failed: %u\n", GetLastError());
      return -1;
   }

   uint64_t real = (ExitTime.uint64 - CreationTime.uint64) / 10000;
   uint64_t system = KernelTime.uint64 / 10000;
   uint64_t user = UserTime.uint64 / 10000;

   fprintf(stderr, "real    %.3f\n", real / 1000.0);
   fprintf(stderr, "system  %.3f\n", system / 1000.0);
   fprintf(stderr, "user    %.3f\n", user / 1000.0);

   return 0;
}
