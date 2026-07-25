$env:Path = "$env:USERPROFILE\.cargo\bin;$env:Path"
& rustc --version
Import-Module "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -DevCmdArguments "-arch=x64" # -SkipAutomaticLocation
# powershell.exe
