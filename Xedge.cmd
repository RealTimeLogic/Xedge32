@echo off

set HOMEDIR=%CD%

IF NOT DEFINED BAS_RESOURCES (
    SET BAS_RESOURCES=main\BAS-Resources
)

set BUILDIR=%BAS_RESOURCES%\build

IF NOT EXIST %BUILDIR%\Xedge.cmd (
   echo Cannot find the BAS-Resources directory. Make sure the directory is in 'main' or set the environment variable BAS_RESOURCES=path
   exit /b 1
)

pushd %BUILDIR%
set NO_BIN2C=no
call Xedge.cmd || exit /b 2
move Xedge.zip %HOMEDIR%\ || exit /b 3
popd

zip -D -q -u -r -9 Xedge.zip .lua || exit /b 4
bin2c -z getLspZipReader Xedge.zip XedgeZip.c || exit /b 5
move XedgeZip.c main\BAS\examples\xedge\obj  || exit /b 5

echo XedgeZip.c copied to main\BAS\examples\xedge\obj


