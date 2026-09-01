@echo off
setlocal enabledelayedexpansion

:: Check if PID is provided
if "%~1"=="" (
    echo Error: Please provide the PID of the target process.
    echo Usage: %~nx0 ^<pid^> "C:\path\to\target" "C:\path\to\replacement" ["C:\path\to\app"]
    exit /b 1
)

:: Check if target path is provided
if "%~2"=="" (
    echo Error: Please provide the target folder.
    echo Usage: %~nx0 ^<pid^> "C:\path\to\target" "C:\path\to\replacement" ["C:\path\to\app"]
    exit /b 1
)
if "%~3"=="" (
    echo Error: Please provide the replacement folder.
    echo Usage: %~nx0 ^<pid^> "C:\path\to\target" "C:\path\to\replacement" ["C:\path\to\app"]
    exit /b 1
)

set "PID=%~1"
set "TIMEOUT=30"
set "ELAPSED=0"

set "TARGET=%~2"
set "REPLACEMENT=%~3"

if "%~4" neq "" (
  set "APP=%~4"
) else (
  set "APP="
)

:: Check if the replacement folder exists
if not exist "%REPLACEMENT%" (
    echo Error: Source folder "%REPLACEMENT%" does not exist.
    exit /b 1
)

:: Check if the target folder exists
if not exist "%TARGET%" (
    echo Error: Destination folder "%TARGET%" does not exist.
    exit /b 1
)

:CHECK_PROCESS
:: Check if the process exists using tasklist
for /f "tokens=1" %%A in ('tasklist /FI "PID eq %PID%" ^| findstr /R "^[0-9]"') do (
    if %ELAPSED% GEQ %TIMEOUT% (
        echo Timeout reached. Exiting.
        exit /b 1
    )
    timeout /t 1 >nul
    set /a ELAPSED+=1
    goto CHECK_PROCESS
)

echo Process with PID %PID% has terminated.

:: Delete files installed by the previous version from the target folder
echo Deleting files installed by the previous version from "%TARGET%"...
rem TB_DELETE_INSTALLED_FILES_BEGIN
if exist "%TARGET%\FreeImage.dll" del /f /q "%TARGET%\FreeImage.dll" && echo %TARGET%\FreeImage.dll
if exist "%TARGET%\Iex-3_3.dll" del /f /q "%TARGET%\Iex-3_3.dll" && echo %TARGET%\Iex-3_3.dll
if exist "%TARGET%\IlmThread-3_3.dll" del /f /q "%TARGET%\IlmThread-3_3.dll" && echo %TARGET%\IlmThread-3_3.dll
if exist "%TARGET%\Imath-3_2.dll" del /f /q "%TARGET%\Imath-3_2.dll" && echo %TARGET%\Imath-3_2.dll
if exist "%TARGET%\OpenEXR-3_3.dll" del /f /q "%TARGET%\OpenEXR-3_3.dll" && echo %TARGET%\OpenEXR-3_3.dll
if exist "%TARGET%\OpenEXRCore-3_3.dll" del /f /q "%TARGET%\OpenEXRCore-3_3.dll" && echo %TARGET%\OpenEXRCore-3_3.dll
if exist "%TARGET%\Qt6Core.dll" del /f /q "%TARGET%\Qt6Core.dll" && echo %TARGET%\Qt6Core.dll
if exist "%TARGET%\Qt6Gui.dll" del /f /q "%TARGET%\Qt6Gui.dll" && echo %TARGET%\Qt6Gui.dll
if exist "%TARGET%\Qt6Network.dll" del /f /q "%TARGET%\Qt6Network.dll" && echo %TARGET%\Qt6Network.dll
if exist "%TARGET%\Qt6OpenGL.dll" del /f /q "%TARGET%\Qt6OpenGL.dll" && echo %TARGET%\Qt6OpenGL.dll
if exist "%TARGET%\Qt6OpenGLWidgets.dll" del /f /q "%TARGET%\Qt6OpenGLWidgets.dll" && echo %TARGET%\Qt6OpenGLWidgets.dll
if exist "%TARGET%\Qt6Svg.dll" del /f /q "%TARGET%\Qt6Svg.dll" && echo %TARGET%\Qt6Svg.dll
if exist "%TARGET%\Qt6Widgets.dll" del /f /q "%TARGET%\Qt6Widgets.dll" && echo %TARGET%\Qt6Widgets.dll
if exist "%TARGET%\TrenchBroom-stripped.pdb" del /f /q "%TARGET%\TrenchBroom-stripped.pdb" && echo %TARGET%\TrenchBroom-stripped.pdb
if exist "%TARGET%\TrenchBroom.exe" del /f /q "%TARGET%\TrenchBroom.exe" && echo %TARGET%\TrenchBroom.exe
if exist "%TARGET%\assimp-vc143-mt.dll" del /f /q "%TARGET%\assimp-vc143-mt.dll" && echo %TARGET%\assimp-vc143-mt.dll
if exist "%TARGET%\brotlicommon.dll" del /f /q "%TARGET%\brotlicommon.dll" && echo %TARGET%\brotlicommon.dll
if exist "%TARGET%\brotlidec.dll" del /f /q "%TARGET%\brotlidec.dll" && echo %TARGET%\brotlidec.dll
if exist "%TARGET%\bz2.dll" del /f /q "%TARGET%\bz2.dll" && echo %TARGET%\bz2.dll
if exist "%TARGET%\cpptrace.dll" del /f /q "%TARGET%\cpptrace.dll" && echo %TARGET%\cpptrace.dll
if exist "%TARGET%\defaults\assets\textures\__TB_empty.png" del /f /q "%TARGET%\defaults\assets\textures\__TB_empty.png" && echo %TARGET%\defaults\assets\textures\__TB_empty.png
if exist "%TARGET%\deflate.dll" del /f /q "%TARGET%\deflate.dll" && echo %TARGET%\deflate.dll
if exist "%TARGET%\fonts\SIL Open Font License.txt" del /f /q "%TARGET%\fonts\SIL Open Font License.txt" && echo %TARGET%\fonts\SIL Open Font License.txt
if exist "%TARGET%\fonts\SourceSansPro-Regular.otf" del /f /q "%TARGET%\fonts\SourceSansPro-Regular.otf" && echo %TARGET%\fonts\SourceSansPro-Regular.otf
if exist "%TARGET%\freetype.dll" del /f /q "%TARGET%\freetype.dll" && echo %TARGET%\freetype.dll
if exist "%TARGET%\games\DDayNormandy\GameConfig.cfg" del /f /q "%TARGET%\games\DDayNormandy\GameConfig.cfg" && echo %TARGET%\games\DDayNormandy\GameConfig.cfg
if exist "%TARGET%\games\DDayNormandy\Icon.png" del /f /q "%TARGET%\games\DDayNormandy\Icon.png" && echo %TARGET%\games\DDayNormandy\Icon.png
if exist "%TARGET%\games\DDayNormandy\colormap.pcx" del /f /q "%TARGET%\games\DDayNormandy\colormap.pcx" && echo %TARGET%\games\DDayNormandy\colormap.pcx
if exist "%TARGET%\games\DDayNormandy\dday.fgd" del /f /q "%TARGET%\games\DDayNormandy\dday.fgd" && echo %TARGET%\games\DDayNormandy\dday.fgd
if exist "%TARGET%\games\Daikatana\Common.fgd" del /f /q "%TARGET%\games\Daikatana\Common.fgd" && echo %TARGET%\games\Daikatana\Common.fgd
if exist "%TARGET%\games\Daikatana\Episode 1.fgd" del /f /q "%TARGET%\games\Daikatana\Episode 1.fgd" && echo %TARGET%\games\Daikatana\Episode 1.fgd
if exist "%TARGET%\games\Daikatana\Episode 2.fgd" del /f /q "%TARGET%\games\Daikatana\Episode 2.fgd" && echo %TARGET%\games\Daikatana\Episode 2.fgd
if exist "%TARGET%\games\Daikatana\Episode 3.fgd" del /f /q "%TARGET%\games\Daikatana\Episode 3.fgd" && echo %TARGET%\games\Daikatana\Episode 3.fgd
if exist "%TARGET%\games\Daikatana\Episode 4.fgd" del /f /q "%TARGET%\games\Daikatana\Episode 4.fgd" && echo %TARGET%\games\Daikatana\Episode 4.fgd
if exist "%TARGET%\games\Daikatana\GameConfig.cfg" del /f /q "%TARGET%\games\Daikatana\GameConfig.cfg" && echo %TARGET%\games\Daikatana\GameConfig.cfg
if exist "%TARGET%\games\Daikatana\Icon.png" del /f /q "%TARGET%\games\Daikatana\Icon.png" && echo %TARGET%\games\Daikatana\Icon.png
if exist "%TARGET%\games\Daikatana\colormap.bmp" del /f /q "%TARGET%\games\Daikatana\colormap.bmp" && echo %TARGET%\games\Daikatana\colormap.bmp
if exist "%TARGET%\games\DigitalPaintball2\GameConfig.cfg" del /f /q "%TARGET%\games\DigitalPaintball2\GameConfig.cfg" && echo %TARGET%\games\DigitalPaintball2\GameConfig.cfg
if exist "%TARGET%\games\DigitalPaintball2\Icon.png" del /f /q "%TARGET%\games\DigitalPaintball2\Icon.png" && echo %TARGET%\games\DigitalPaintball2\Icon.png
if exist "%TARGET%\games\DigitalPaintball2\pball2.fgd" del /f /q "%TARGET%\games\DigitalPaintball2\pball2.fgd" && echo %TARGET%\games\DigitalPaintball2\pball2.fgd
if exist "%TARGET%\games\Generic\GameConfig.cfg" del /f /q "%TARGET%\games\Generic\GameConfig.cfg" && echo %TARGET%\games\Generic\GameConfig.cfg
if exist "%TARGET%\games\Generic\Generic.fgd" del /f /q "%TARGET%\games\Generic\Generic.fgd" && echo %TARGET%\games\Generic\Generic.fgd
if exist "%TARGET%\games\Generic\Icon.png" del /f /q "%TARGET%\games\Generic\Icon.png" && echo %TARGET%\games\Generic\Icon.png
if exist "%TARGET%\games\Halflife\GameConfig.cfg" del /f /q "%TARGET%\games\Halflife\GameConfig.cfg" && echo %TARGET%\games\Halflife\GameConfig.cfg
if exist "%TARGET%\games\Halflife\HalfLife.fgd" del /f /q "%TARGET%\games\Halflife\HalfLife.fgd" && echo %TARGET%\games\Halflife\HalfLife.fgd
if exist "%TARGET%\games\Halflife\Icon.png" del /f /q "%TARGET%\games\Halflife\Icon.png" && echo %TARGET%\games\Halflife\Icon.png
if exist "%TARGET%\games\Heretic2\GameConfig.cfg" del /f /q "%TARGET%\games\Heretic2\GameConfig.cfg" && echo %TARGET%\games\Heretic2\GameConfig.cfg
if exist "%TARGET%\games\Heretic2\Icon.png" del /f /q "%TARGET%\games\Heretic2\Icon.png" && echo %TARGET%\games\Heretic2\Icon.png
if exist "%TARGET%\games\Heretic2\heretic2.fgd" del /f /q "%TARGET%\games\Heretic2\heretic2.fgd" && echo %TARGET%\games\Heretic2\heretic2.fgd
if exist "%TARGET%\games\Hexen2\GameConfig.cfg" del /f /q "%TARGET%\games\Hexen2\GameConfig.cfg" && echo %TARGET%\games\Hexen2\GameConfig.cfg
if exist "%TARGET%\games\Hexen2\Hexen2.fgd" del /f /q "%TARGET%\games\Hexen2\Hexen2.fgd" && echo %TARGET%\games\Hexen2\Hexen2.fgd
if exist "%TARGET%\games\Hexen2\Icon.png" del /f /q "%TARGET%\games\Hexen2\Icon.png" && echo %TARGET%\games\Hexen2\Icon.png
if exist "%TARGET%\games\Hexen2\palette.lmp" del /f /q "%TARGET%\games\Hexen2\palette.lmp" && echo %TARGET%\games\Hexen2\palette.lmp
if exist "%TARGET%\games\Kingpin\GameConfig.cfg" del /f /q "%TARGET%\games\Kingpin\GameConfig.cfg" && echo %TARGET%\games\Kingpin\GameConfig.cfg
if exist "%TARGET%\games\Kingpin\Icon.png" del /f /q "%TARGET%\games\Kingpin\Icon.png" && echo %TARGET%\games\Kingpin\Icon.png
if exist "%TARGET%\games\Kingpin\colormap.pcx" del /f /q "%TARGET%\games\Kingpin\colormap.pcx" && echo %TARGET%\games\Kingpin\colormap.pcx
if exist "%TARGET%\games\Kingpin\kingpin.fgd" del /f /q "%TARGET%\games\Kingpin\kingpin.fgd" && echo %TARGET%\games\Kingpin\kingpin.fgd
if exist "%TARGET%\games\Neverball\GameConfig.cfg" del /f /q "%TARGET%\games\Neverball\GameConfig.cfg" && echo %TARGET%\games\Neverball\GameConfig.cfg
if exist "%TARGET%\games\Neverball\Icon.png" del /f /q "%TARGET%\games\Neverball\Icon.png" && echo %TARGET%\games\Neverball\Icon.png
if exist "%TARGET%\games\Neverball\assets\__tb_generic_ring_05.obj" del /f /q "%TARGET%\games\Neverball\assets\__tb_generic_ring_05.obj" && echo %TARGET%\games\Neverball\assets\__tb_generic_ring_05.obj
if exist "%TARGET%\games\Neverball\assets\__tb_info_player_deathmatch.obj" del /f /q "%TARGET%\games\Neverball\assets\__tb_info_player_deathmatch.obj" && echo %TARGET%\games\Neverball\assets\__tb_info_player_deathmatch.obj
if exist "%TARGET%\games\Neverball\assets\__tb_info_player_start.obj" del /f /q "%TARGET%\games\Neverball\assets\__tb_info_player_start.obj" && echo %TARGET%\games\Neverball\assets\__tb_info_player_start.obj
if exist "%TARGET%\games\Neverball\assets\__tb_info_player_start.png" del /f /q "%TARGET%\games\Neverball\assets\__tb_info_player_start.png" && echo %TARGET%\games\Neverball\assets\__tb_info_player_start.png
if exist "%TARGET%\games\Neverball\assets\__tb_item_health_large.obj" del /f /q "%TARGET%\games\Neverball\assets\__tb_item_health_large.obj" && echo %TARGET%\games\Neverball\assets\__tb_item_health_large.obj
if exist "%TARGET%\games\Neverball\assets\__tb_item_health_small.obj" del /f /q "%TARGET%\games\Neverball\assets\__tb_item_health_small.obj" && echo %TARGET%\games\Neverball\assets\__tb_item_health_small.obj
if exist "%TARGET%\games\Neverball\assets\__tb_light_1.obj" del /f /q "%TARGET%\games\Neverball\assets\__tb_light_1.obj" && echo %TARGET%\games\Neverball\assets\__tb_light_1.obj
if exist "%TARGET%\games\Neverball\assets\__tb_light_10.obj" del /f /q "%TARGET%\games\Neverball\assets\__tb_light_10.obj" && echo %TARGET%\games\Neverball\assets\__tb_light_10.obj
if exist "%TARGET%\games\Neverball\assets\__tb_light_5.obj" del /f /q "%TARGET%\games\Neverball\assets\__tb_light_5.obj" && echo %TARGET%\games\Neverball\assets\__tb_light_5.obj
if exist "%TARGET%\games\Neverball\initial_neverball_map.map" del /f /q "%TARGET%\games\Neverball\initial_neverball_map.map" && echo %TARGET%\games\Neverball\initial_neverball_map.map
if exist "%TARGET%\games\Neverball\neverball.fgd" del /f /q "%TARGET%\games\Neverball\neverball.fgd" && echo %TARGET%\games\Neverball\neverball.fgd
if exist "%TARGET%\games\Quake\GameConfig.cfg" del /f /q "%TARGET%\games\Quake\GameConfig.cfg" && echo %TARGET%\games\Quake\GameConfig.cfg
if exist "%TARGET%\games\Quake\Icon.png" del /f /q "%TARGET%\games\Quake\Icon.png" && echo %TARGET%\games\Quake\Icon.png
if exist "%TARGET%\games\Quake\Quake.fgd" del /f /q "%TARGET%\games\Quake\Quake.fgd" && echo %TARGET%\games\Quake\Quake.fgd
if exist "%TARGET%\games\Quake\Quoth2.fgd" del /f /q "%TARGET%\games\Quake\Quoth2.fgd" && echo %TARGET%\games\Quake\Quoth2.fgd
if exist "%TARGET%\games\Quake\Rubicon2.def" del /f /q "%TARGET%\games\Quake\Rubicon2.def" && echo %TARGET%\games\Quake\Rubicon2.def
if exist "%TARGET%\games\Quake\Teamfortress.fgd" del /f /q "%TARGET%\games\Quake\Teamfortress.fgd" && echo %TARGET%\games\Quake\Teamfortress.fgd
if exist "%TARGET%\games\Quake2\GameConfig.cfg" del /f /q "%TARGET%\games\Quake2\GameConfig.cfg" && echo %TARGET%\games\Quake2\GameConfig.cfg
if exist "%TARGET%\games\Quake2\Icon.png" del /f /q "%TARGET%\games\Quake2\Icon.png" && echo %TARGET%\games\Quake2\Icon.png
if exist "%TARGET%\games\Quake2\Quake2.fgd" del /f /q "%TARGET%\games\Quake2\Quake2.fgd" && echo %TARGET%\games\Quake2\Quake2.fgd
if exist "%TARGET%\games\Quake2\colormap.pcx" del /f /q "%TARGET%\games\Quake2\colormap.pcx" && echo %TARGET%\games\Quake2\colormap.pcx
if exist "%TARGET%\games\Quake3\GameConfig.cfg" del /f /q "%TARGET%\games\Quake3\GameConfig.cfg" && echo %TARGET%\games\Quake3\GameConfig.cfg
if exist "%TARGET%\games\Quake3\Icon.png" del /f /q "%TARGET%\games\Quake3\Icon.png" && echo %TARGET%\games\Quake3\Icon.png
if exist "%TARGET%\games\Quake3\entities.ent" del /f /q "%TARGET%\games\Quake3\entities.ent" && echo %TARGET%\games\Quake3\entities.ent
if exist "%TARGET%\games\Quetoo\GameConfig.cfg" del /f /q "%TARGET%\games\Quetoo\GameConfig.cfg" && echo %TARGET%\games\Quetoo\GameConfig.cfg
if exist "%TARGET%\games\Quetoo\Icon.png" del /f /q "%TARGET%\games\Quetoo\Icon.png" && echo %TARGET%\games\Quetoo\Icon.png
if exist "%TARGET%\games\Quetoo\Quetoo.fgd" del /f /q "%TARGET%\games\Quetoo\Quetoo.fgd" && echo %TARGET%\games\Quetoo\Quetoo.fgd
if exist "%TARGET%\games\SoF\GameConfig.cfg" del /f /q "%TARGET%\games\SoF\GameConfig.cfg" && echo %TARGET%\games\SoF\GameConfig.cfg
if exist "%TARGET%\games\SoF\Icon.png" del /f /q "%TARGET%\games\SoF\Icon.png" && echo %TARGET%\games\SoF\Icon.png
if exist "%TARGET%\games\SoF\assets\textures\__tb_empty.m32" del /f /q "%TARGET%\games\SoF\assets\textures\__tb_empty.m32" && echo %TARGET%\games\SoF\assets\textures\__tb_empty.m32
if exist "%TARGET%\games\SoF\sof.def" del /f /q "%TARGET%\games\SoF\sof.def" && echo %TARGET%\games\SoF\sof.def
if exist "%TARGET%\games\Wrath\GameConfig.cfg" del /f /q "%TARGET%\games\Wrath\GameConfig.cfg" && echo %TARGET%\games\Wrath\GameConfig.cfg
if exist "%TARGET%\games\Wrath\Icon.png" del /f /q "%TARGET%\games\Wrath\Icon.png" && echo %TARGET%\games\Wrath\Icon.png
if exist "%TARGET%\games\Wrath\wrath.fgd" del /f /q "%TARGET%\games\Wrath\wrath.fgd" && echo %TARGET%\games\Wrath\wrath.fgd
if exist "%TARGET%\icuuc.dll" del /f /q "%TARGET%\icuuc.dll" && echo %TARGET%\icuuc.dll
if exist "%TARGET%\images\Add.svg" del /f /q "%TARGET%\images\Add.svg" && echo %TARGET%\images\Add.svg
if exist "%TARGET%\images\AddProtected.png" del /f /q "%TARGET%\images\AddProtected.png" && echo %TARGET%\images\AddProtected.png
if exist "%TARGET%\images\AddProtected.svg" del /f /q "%TARGET%\images\AddProtected.svg" && echo %TARGET%\images\AddProtected.svg
if exist "%TARGET%\images\AlignTexture.svg" del /f /q "%TARGET%\images\AlignTexture.svg" && echo %TARGET%\images\AlignTexture.svg
if exist "%TARGET%\images\AlignmentLock_off.svg" del /f /q "%TARGET%\images\AlignmentLock_off.svg" && echo %TARGET%\images\AlignmentLock_off.svg
if exist "%TARGET%\images\AlignmentLock_on.svg" del /f /q "%TARGET%\images\AlignmentLock_on.svg" && echo %TARGET%\images\AlignmentLock_on.svg
if exist "%TARGET%\images\AppIcon.png" del /f /q "%TARGET%\images\AppIcon.png" && echo %TARGET%\images\AppIcon.png
if exist "%TARGET%\images\AutoFitTexture.svg" del /f /q "%TARGET%\images\AutoFitTexture.svg" && echo %TARGET%\images\AutoFitTexture.svg
if exist "%TARGET%\images\BrushTool.svg" del /f /q "%TARGET%\images\BrushTool.svg" && echo %TARGET%\images\BrushTool.svg
if exist "%TARGET%\images\CircleEdgeAligned.svg" del /f /q "%TARGET%\images\CircleEdgeAligned.svg" && echo %TARGET%\images\CircleEdgeAligned.svg
if exist "%TARGET%\images\CircleScalable.svg" del /f /q "%TARGET%\images\CircleScalable.svg" && echo %TARGET%\images\CircleScalable.svg
if exist "%TARGET%\images\CircleVertexAligned.svg" del /f /q "%TARGET%\images\CircleVertexAligned.svg" && echo %TARGET%\images\CircleVertexAligned.svg
if exist "%TARGET%\images\ClipTool.svg" del /f /q "%TARGET%\images\ClipTool.svg" && echo %TARGET%\images\ClipTool.svg
if exist "%TARGET%\images\ColorPreferences.svg" del /f /q "%TARGET%\images\ColorPreferences.svg" && echo %TARGET%\images\ColorPreferences.svg
if exist "%TARGET%\images\Conflict.svg" del /f /q "%TARGET%\images\Conflict.svg" && echo %TARGET%\images\Conflict.svg
if exist "%TARGET%\images\ControlPointTool.svg" del /f /q "%TARGET%\images\ControlPointTool.svg" && echo %TARGET%\images\ControlPointTool.svg
if exist "%TARGET%\images\DefaultGameIcon.svg" del /f /q "%TARGET%\images\DefaultGameIcon.svg" && echo %TARGET%\images\DefaultGameIcon.svg
if exist "%TARGET%\images\DocIcon.png" del /f /q "%TARGET%\images\DocIcon.png" && echo %TARGET%\images\DocIcon.png
if exist "%TARGET%\images\Down.svg" del /f /q "%TARGET%\images\Down.svg" && echo %TARGET%\images\Down.svg
if exist "%TARGET%\images\DuplicateObjects.svg" del /f /q "%TARGET%\images\DuplicateObjects.svg" && echo %TARGET%\images\DuplicateObjects.svg
if exist "%TARGET%\images\EdgeTool.svg" del /f /q "%TARGET%\images\EdgeTool.svg" && echo %TARGET%\images\EdgeTool.svg
if exist "%TARGET%\images\FaceTool.svg" del /f /q "%TARGET%\images\FaceTool.svg" && echo %TARGET%\images\FaceTool.svg
if exist "%TARGET%\images\FitTextureHorizontally.svg" del /f /q "%TARGET%\images\FitTextureHorizontally.svg" && echo %TARGET%\images\FitTextureHorizontally.svg
if exist "%TARGET%\images\FitTextureVertically.svg" del /f /q "%TARGET%\images\FitTextureVertically.svg" && echo %TARGET%\images\FitTextureVertically.svg
if exist "%TARGET%\images\FlipHorizontally.svg" del /f /q "%TARGET%\images\FlipHorizontally.svg" && echo %TARGET%\images\FlipHorizontally.svg
if exist "%TARGET%\images\FlipUAxis.svg" del /f /q "%TARGET%\images\FlipUAxis.svg" && echo %TARGET%\images\FlipUAxis.svg
if exist "%TARGET%\images\FlipVAxis.svg" del /f /q "%TARGET%\images\FlipVAxis.svg" && echo %TARGET%\images\FlipVAxis.svg
if exist "%TARGET%\images\FlipVertically.svg" del /f /q "%TARGET%\images\FlipVertically.svg" && echo %TARGET%\images\FlipVertically.svg
if exist "%TARGET%\images\Folder.svg" del /f /q "%TARGET%\images\Folder.svg" && echo %TARGET%\images\Folder.svg
if exist "%TARGET%\images\GeneralPreferences.svg" del /f /q "%TARGET%\images\GeneralPreferences.svg" && echo %TARGET%\images\GeneralPreferences.svg
if exist "%TARGET%\images\Hidden_off.svg" del /f /q "%TARGET%\images\Hidden_off.svg" && echo %TARGET%\images\Hidden_off.svg
if exist "%TARGET%\images\Hidden_on.svg" del /f /q "%TARGET%\images\Hidden_on.svg" && echo %TARGET%\images\Hidden_on.svg
if exist "%TARGET%\images\IssueBrowser.svg" del /f /q "%TARGET%\images\IssueBrowser.svg" && echo %TARGET%\images\IssueBrowser.svg
if exist "%TARGET%\images\JustifyTextureDown.svg" del /f /q "%TARGET%\images\JustifyTextureDown.svg" && echo %TARGET%\images\JustifyTextureDown.svg
if exist "%TARGET%\images\JustifyTextureLeft.svg" del /f /q "%TARGET%\images\JustifyTextureLeft.svg" && echo %TARGET%\images\JustifyTextureLeft.svg
if exist "%TARGET%\images\JustifyTextureRight.svg" del /f /q "%TARGET%\images\JustifyTextureRight.svg" && echo %TARGET%\images\JustifyTextureRight.svg
if exist "%TARGET%\images\JustifyTextureUp.svg" del /f /q "%TARGET%\images\JustifyTextureUp.svg" && echo %TARGET%\images\JustifyTextureUp.svg
if exist "%TARGET%\images\KeyboardPreferences.svg" del /f /q "%TARGET%\images\KeyboardPreferences.svg" && echo %TARGET%\images\KeyboardPreferences.svg
if exist "%TARGET%\images\Lock_off.svg" del /f /q "%TARGET%\images\Lock_off.svg" && echo %TARGET%\images\Lock_off.svg
if exist "%TARGET%\images\Lock_on.svg" del /f /q "%TARGET%\images\Lock_on.svg" && echo %TARGET%\images\Lock_on.svg
if exist "%TARGET%\images\Locked_small.svg" del /f /q "%TARGET%\images\Locked_small.svg" && echo %TARGET%\images\Locked_small.svg
if exist "%TARGET%\images\MousePreferences.svg" del /f /q "%TARGET%\images\MousePreferences.svg" && echo %TARGET%\images\MousePreferences.svg
if exist "%TARGET%\images\NoTool.svg" del /f /q "%TARGET%\images\NoTool.svg" && echo %TARGET%\images\NoTool.svg
if exist "%TARGET%\images\OmitFromExport_off.svg" del /f /q "%TARGET%\images\OmitFromExport_off.svg" && echo %TARGET%\images\OmitFromExport_off.svg
if exist "%TARGET%\images\OmitFromExport_on.svg" del /f /q "%TARGET%\images\OmitFromExport_on.svg" && echo %TARGET%\images\OmitFromExport_on.svg
if exist "%TARGET%\images\Protected_small.svg" del /f /q "%TARGET%\images\Protected_small.svg" && echo %TARGET%\images\Protected_small.svg
if exist "%TARGET%\images\Refresh.svg" del /f /q "%TARGET%\images\Refresh.svg" && echo %TARGET%\images\Refresh.svg
if exist "%TARGET%\images\Remove.svg" del /f /q "%TARGET%\images\Remove.svg" && echo %TARGET%\images\Remove.svg
if exist "%TARGET%\images\ResetUV.svg" del /f /q "%TARGET%\images\ResetUV.svg" && echo %TARGET%\images\ResetUV.svg
if exist "%TARGET%\images\ResetUVToWorld.svg" del /f /q "%TARGET%\images\ResetUVToWorld.svg" && echo %TARGET%\images\ResetUVToWorld.svg
if exist "%TARGET%\images\RotateTool.svg" del /f /q "%TARGET%\images\RotateTool.svg" && echo %TARGET%\images\RotateTool.svg
if exist "%TARGET%\images\RotateUVCCW.svg" del /f /q "%TARGET%\images\RotateUVCCW.svg" && echo %TARGET%\images\RotateUVCCW.svg
if exist "%TARGET%\images\RotateUVCW.svg" del /f /q "%TARGET%\images\RotateUVCW.svg" && echo %TARGET%\images\RotateUVCW.svg
if exist "%TARGET%\images\ScaleTool.svg" del /f /q "%TARGET%\images\ScaleTool.svg" && echo %TARGET%\images\ScaleTool.svg
if exist "%TARGET%\images\Search.svg" del /f /q "%TARGET%\images\Search.svg" && echo %TARGET%\images\Search.svg
if exist "%TARGET%\images\SetDefaultProperties.svg" del /f /q "%TARGET%\images\SetDefaultProperties.svg" && echo %TARGET%\images\SetDefaultProperties.svg
if exist "%TARGET%\images\ShapeTool_Arch.svg" del /f /q "%TARGET%\images\ShapeTool_Arch.svg" && echo %TARGET%\images\ShapeTool_Arch.svg
if exist "%TARGET%\images\ShapeTool_Cone.svg" del /f /q "%TARGET%\images\ShapeTool_Cone.svg" && echo %TARGET%\images\ShapeTool_Cone.svg
if exist "%TARGET%\images\ShapeTool_Cuboid.svg" del /f /q "%TARGET%\images\ShapeTool_Cuboid.svg" && echo %TARGET%\images\ShapeTool_Cuboid.svg
if exist "%TARGET%\images\ShapeTool_Cylinder.svg" del /f /q "%TARGET%\images\ShapeTool_Cylinder.svg" && echo %TARGET%\images\ShapeTool_Cylinder.svg
if exist "%TARGET%\images\ShapeTool_IcoSphere.svg" del /f /q "%TARGET%\images\ShapeTool_IcoSphere.svg" && echo %TARGET%\images\ShapeTool_IcoSphere.svg
if exist "%TARGET%\images\ShapeTool_Stairs.svg" del /f /q "%TARGET%\images\ShapeTool_Stairs.svg" && echo %TARGET%\images\ShapeTool_Stairs.svg
if exist "%TARGET%\images\ShapeTool_UVSphere.svg" del /f /q "%TARGET%\images\ShapeTool_UVSphere.svg" && echo %TARGET%\images\ShapeTool_UVSphere.svg
if exist "%TARGET%\images\ShearTool.svg" del /f /q "%TARGET%\images\ShearTool.svg" && echo %TARGET%\images\ShearTool.svg
if exist "%TARGET%\images\SweepTool.svg" del /f /q "%TARGET%\images\SweepTool.svg" && echo %TARGET%\images\SweepTool.svg
if exist "%TARGET%\images\UVLock_off.svg" del /f /q "%TARGET%\images\UVLock_off.svg" && echo %TARGET%\images\UVLock_off.svg
if exist "%TARGET%\images\UVLock_on.svg" del /f /q "%TARGET%\images\UVLock_on.svg" && echo %TARGET%\images\UVLock_on.svg
if exist "%TARGET%\images\Up.svg" del /f /q "%TARGET%\images\Up.svg" && echo %TARGET%\images\Up.svg
if exist "%TARGET%\images\UpdatePreferences.svg" del /f /q "%TARGET%\images\UpdatePreferences.svg" && echo %TARGET%\images\UpdatePreferences.svg
if exist "%TARGET%\images\VertexTool.svg" del /f /q "%TARGET%\images\VertexTool.svg" && echo %TARGET%\images\VertexTool.svg
if exist "%TARGET%\images\ViewPreferences.svg" del /f /q "%TARGET%\images\ViewPreferences.svg" && echo %TARGET%\images\ViewPreferences.svg
if exist "%TARGET%\images\credits.md" del /f /q "%TARGET%\images\credits.md" && echo %TARGET%\images\credits.md
if exist "%TARGET%\jpeg8.dll" del /f /q "%TARGET%\jpeg8.dll" && echo %TARGET%\jpeg8.dll
if exist "%TARGET%\kubazip.dll" del /f /q "%TARGET%\kubazip.dll" && echo %TARGET%\kubazip.dll
if exist "%TARGET%\lcms2-2.dll" del /f /q "%TARGET%\lcms2-2.dll" && echo %TARGET%\lcms2-2.dll
if exist "%TARGET%\liblzma.dll" del /f /q "%TARGET%\liblzma.dll" && echo %TARGET%\liblzma.dll
if exist "%TARGET%\libpng16.dll" del /f /q "%TARGET%\libpng16.dll" && echo %TARGET%\libpng16.dll
if exist "%TARGET%\libsharpyuv.dll" del /f /q "%TARGET%\libsharpyuv.dll" && echo %TARGET%\libsharpyuv.dll
if exist "%TARGET%\libwebp.dll" del /f /q "%TARGET%\libwebp.dll" && echo %TARGET%\libwebp.dll
if exist "%TARGET%\libwebpdecoder.dll" del /f /q "%TARGET%\libwebpdecoder.dll" && echo %TARGET%\libwebpdecoder.dll
if exist "%TARGET%\libwebpmux.dll" del /f /q "%TARGET%\libwebpmux.dll" && echo %TARGET%\libwebpmux.dll
if exist "%TARGET%\manual\_reset.css" del /f /q "%TARGET%\manual\_reset.css" && echo %TARGET%\manual\_reset.css
if exist "%TARGET%\manual\_responsive.css" del /f /q "%TARGET%\manual\_responsive.css" && echo %TARGET%\manual\_responsive.css
if exist "%TARGET%\manual\_specific.css" del /f /q "%TARGET%\manual\_specific.css" && echo %TARGET%\manual\_specific.css
if exist "%TARGET%\manual\_specific.darkmode.css" del /f /q "%TARGET%\manual\_specific.darkmode.css" && echo %TARGET%\manual\_specific.darkmode.css
if exist "%TARGET%\manual\_variables.css" del /f /q "%TARGET%\manual\_variables.css" && echo %TARGET%\manual\_variables.css
if exist "%TARGET%\manual\_variables.darkmode.css" del /f /q "%TARGET%\manual\_variables.darkmode.css" && echo %TARGET%\manual\_variables.darkmode.css
if exist "%TARGET%\manual\default.css" del /f /q "%TARGET%\manual\default.css" && echo %TARGET%\manual\default.css
if exist "%TARGET%\manual\default.darkmode.css" del /f /q "%TARGET%\manual\default.darkmode.css" && echo %TARGET%\manual\default.darkmode.css
if exist "%TARGET%\manual\images\AlignJustifyFit.png" del /f /q "%TARGET%\manual\images\AlignJustifyFit.png" && echo %TARGET%\manual\images\AlignJustifyFit.png
if exist "%TARGET%\manual\images\AxisRestriction.png" del /f /q "%TARGET%\manual\images\AxisRestriction.png" && echo %TARGET%\manual\images\AxisRestriction.png
if exist "%TARGET%\manual\images\BrushFaceSelection.png" del /f /q "%TARGET%\manual\images\BrushFaceSelection.png" && echo %TARGET%\manual\images\BrushFaceSelection.png
if exist "%TARGET%\manual\images\CSGConvexMerge.gif" del /f /q "%TARGET%\manual\images\CSGConvexMerge.gif" && echo %TARGET%\manual\images\CSGConvexMerge.gif
if exist "%TARGET%\manual\images\CSGHollow.gif" del /f /q "%TARGET%\manual\images\CSGHollow.gif" && echo %TARGET%\manual\images\CSGHollow.gif
if exist "%TARGET%\manual\images\CSGIntersect.gif" del /f /q "%TARGET%\manual\images\CSGIntersect.gif" && echo %TARGET%\manual\images\CSGIntersect.gif
if exist "%TARGET%\manual\images\CSGSubtractArch.gif" del /f /q "%TARGET%\manual\images\CSGSubtractArch.gif" && echo %TARGET%\manual\images\CSGSubtractArch.gif
if exist "%TARGET%\manual\images\CSGTexturing.gif" del /f /q "%TARGET%\manual\images\CSGTexturing.gif" && echo %TARGET%\manual\images\CSGTexturing.gif
if exist "%TARGET%\manual\images\CTF1Bounds.png" del /f /q "%TARGET%\manual\images\CTF1Bounds.png" && echo %TARGET%\manual\images\CTF1Bounds.png
if exist "%TARGET%\manual\images\CircleEdgeAligned.png" del /f /q "%TARGET%\manual\images\CircleEdgeAligned.png" && echo %TARGET%\manual\images\CircleEdgeAligned.png
if exist "%TARGET%\manual\images\CircleScalable.png" del /f /q "%TARGET%\manual\images\CircleScalable.png" && echo %TARGET%\manual\images\CircleScalable.png
if exist "%TARGET%\manual\images\CircleVertexAligned.png" del /f /q "%TARGET%\manual\images\CircleVertexAligned.png" && echo %TARGET%\manual\images\CircleVertexAligned.png
if exist "%TARGET%\manual\images\ClipModes.png" del /f /q "%TARGET%\manual\images\ClipModes.png" && echo %TARGET%\manual\images\ClipModes.png
if exist "%TARGET%\manual\images\Compass3D.png" del /f /q "%TARGET%\manual\images\Compass3D.png" && echo %TARGET%\manual\images\Compass3D.png
if exist "%TARGET%\manual\images\CompilationDialog.png" del /f /q "%TARGET%\manual\images\CompilationDialog.png" && echo %TARGET%\manual\images\CompilationDialog.png
if exist "%TARGET%\manual\images\CompilationDialogToolVars.png" del /f /q "%TARGET%\manual\images\CompilationDialogToolVars.png" && echo %TARGET%\manual\images\CompilationDialogToolVars.png
if exist "%TARGET%\manual\images\ControlPointTool.png" del /f /q "%TARGET%\manual\images\ControlPointTool.png" && echo %TARGET%\manual\images\ControlPointTool.png
if exist "%TARGET%\manual\images\ControlPointToolSpinBoxes.png" del /f /q "%TARGET%\manual\images\ControlPointToolSpinBoxes.png" && echo %TARGET%\manual\images\ControlPointToolSpinBoxes.png
if exist "%TARGET%\manual\images\CreateBrushByDuplicatingPolygon.gif" del /f /q "%TARGET%\manual\images\CreateBrushByDuplicatingPolygon.gif" && echo %TARGET%\manual\images\CreateBrushByDuplicatingPolygon.gif
if exist "%TARGET%\manual\images\CreateEntityContextMenu.png" del /f /q "%TARGET%\manual\images\CreateEntityContextMenu.png" && echo %TARGET%\manual\images\CreateEntityContextMenu.png
if exist "%TARGET%\manual\images\CreatePatches_Corner.gif" del /f /q "%TARGET%\manual\images\CreatePatches_Corner.gif" && echo %TARGET%\manual\images\CreatePatches_Corner.gif
if exist "%TARGET%\manual\images\CreatePatches_Octagon.gif" del /f /q "%TARGET%\manual\images\CreatePatches_Octagon.gif" && echo %TARGET%\manual\images\CreatePatches_Octagon.gif
if exist "%TARGET%\manual\images\CreatePatches_Square.gif" del /f /q "%TARGET%\manual\images\CreatePatches_Square.gif" && echo %TARGET%\manual\images\CreatePatches_Square.gif
if exist "%TARGET%\manual\images\DrawBrush.gif" del /f /q "%TARGET%\manual\images\DrawBrush.gif" && echo %TARGET%\manual\images\DrawBrush.gif
if exist "%TARGET%\manual\images\DrillSelection.gif" del /f /q "%TARGET%\manual\images\DrillSelection.gif" && echo %TARGET%\manual\images\DrillSelection.gif
if exist "%TARGET%\manual\images\DuplicateAndMove.gif" del /f /q "%TARGET%\manual\images\DuplicateAndMove.gif" && echo %TARGET%\manual\images\DuplicateAndMove.gif
if exist "%TARGET%\manual\images\DuplicateInPlace.gif" del /f /q "%TARGET%\manual\images\DuplicateInPlace.gif" && echo %TARGET%\manual\images\DuplicateInPlace.gif
if exist "%TARGET%\manual\images\EdgeTool.png" del /f /q "%TARGET%\manual\images\EdgeTool.png" && echo %TARGET%\manual\images\EdgeTool.png
if exist "%TARGET%\manual\images\EntityBrowser.png" del /f /q "%TARGET%\manual\images\EntityBrowser.png" && echo %TARGET%\manual\images\EntityBrowser.png
if exist "%TARGET%\manual\images\EntityDefinitionEditor.png" del /f /q "%TARGET%\manual\images\EntityDefinitionEditor.png" && echo %TARGET%\manual\images\EntityDefinitionEditor.png
if exist "%TARGET%\manual\images\EntityLinkVisualization.png" del /f /q "%TARGET%\manual\images\EntityLinkVisualization.png" && echo %TARGET%\manual\images\EntityLinkVisualization.png
if exist "%TARGET%\manual\images\EntityPropertyEditor.png" del /f /q "%TARGET%\manual\images\EntityPropertyEditor.png" && echo %TARGET%\manual\images\EntityPropertyEditor.png
if exist "%TARGET%\manual\images\EntityPropertyEditorMultiSelection.png" del /f /q "%TARGET%\manual\images\EntityPropertyEditorMultiSelection.png" && echo %TARGET%\manual\images\EntityPropertyEditorMultiSelection.png
if exist "%TARGET%\manual\images\ExtrudeTool2DFaceMoving.gif" del /f /q "%TARGET%\manual\images\ExtrudeTool2DFaceMoving.gif" && echo %TARGET%\manual\images\ExtrudeTool2DFaceMoving.gif
if exist "%TARGET%\manual\images\ExtrudeTool3D.gif" del /f /q "%TARGET%\manual\images\ExtrudeTool3D.gif" && echo %TARGET%\manual\images\ExtrudeTool3D.gif
if exist "%TARGET%\manual\images\ExtrudeTool3DMultipleBrushes.gif" del /f /q "%TARGET%\manual\images\ExtrudeTool3DMultipleBrushes.gif" && echo %TARGET%\manual\images\ExtrudeTool3DMultipleBrushes.gif
if exist "%TARGET%\manual\images\ExtrudeTool3DSplitInwardMode.gif" del /f /q "%TARGET%\manual\images\ExtrudeTool3DSplitInwardMode.gif" && echo %TARGET%\manual\images\ExtrudeTool3DSplitInwardMode.gif
if exist "%TARGET%\manual\images\ExtrudeTool3DSplitMode.gif" del /f /q "%TARGET%\manual\images\ExtrudeTool3DSplitMode.gif" && echo %TARGET%\manual\images\ExtrudeTool3DSplitMode.gif
if exist "%TARGET%\manual\images\FaceAttribsEditor.png" del /f /q "%TARGET%\manual\images\FaceAttribsEditor.png" && echo %TARGET%\manual\images\FaceAttribsEditor.png
if exist "%TARGET%\manual\images\FaceTool.png" del /f /q "%TARGET%\manual\images\FaceTool.png" && echo %TARGET%\manual\images\FaceTool.png
if exist "%TARGET%\manual\images\GameEngineDialog.png" del /f /q "%TARGET%\manual\images\GameEngineDialog.png" && echo %TARGET%\manual\images\GameEngineDialog.png
if exist "%TARGET%\manual\images\GamePreferences.png" del /f /q "%TARGET%\manual\images\GamePreferences.png" && echo %TARGET%\manual\images\GamePreferences.png
if exist "%TARGET%\manual\images\GameSelectionDialog.png" del /f /q "%TARGET%\manual\images\GameSelectionDialog.png" && echo %TARGET%\manual\images\GameSelectionDialog.png
if exist "%TARGET%\manual\images\Inspector.png" del /f /q "%TARGET%\manual\images\Inspector.png" && echo %TARGET%\manual\images\Inspector.png
if exist "%TARGET%\manual\images\IssueBrowserContextMenu.png" del /f /q "%TARGET%\manual\images\IssueBrowserContextMenu.png" && echo %TARGET%\manual\images\IssueBrowserContextMenu.png
if exist "%TARGET%\manual\images\IssueBrowserFilter.png" del /f /q "%TARGET%\manual\images\IssueBrowserFilter.png" && echo %TARGET%\manual\images\IssueBrowserFilter.png
if exist "%TARGET%\manual\images\KeyboardPreferences.png" del /f /q "%TARGET%\manual\images\KeyboardPreferences.png" && echo %TARGET%\manual\images\KeyboardPreferences.png
if exist "%TARGET%\manual\images\LaunchGameEngineDialog.png" del /f /q "%TARGET%\manual\images\LaunchGameEngineDialog.png" && echo %TARGET%\manual\images\LaunchGameEngineDialog.png
if exist "%TARGET%\manual\images\LayerEditor.png" del /f /q "%TARGET%\manual\images\LayerEditor.png" && echo %TARGET%\manual\images\LayerEditor.png
if exist "%TARGET%\manual\images\Linked2DViewports.gif" del /f /q "%TARGET%\manual\images\Linked2DViewports.gif" && echo %TARGET%\manual\images\Linked2DViewports.gif
if exist "%TARGET%\manual\images\LinkedGroups.png" del /f /q "%TARGET%\manual\images\LinkedGroups.png" && echo %TARGET%\manual\images\LinkedGroups.png
if exist "%TARGET%\manual\images\Locking.png" del /f /q "%TARGET%\manual\images\Locking.png" && echo %TARGET%\manual\images\Locking.png
if exist "%TARGET%\manual\images\MainWindow.png" del /f /q "%TARGET%\manual\images\MainWindow.png" && echo %TARGET%\manual\images\MainWindow.png
if exist "%TARGET%\manual\images\MatchingClipPlane.gif" del /f /q "%TARGET%\manual\images\MatchingClipPlane.gif" && echo %TARGET%\manual\images\MatchingClipPlane.gif
if exist "%TARGET%\manual\images\ModEditor.png" del /f /q "%TARGET%\manual\images\ModEditor.png" && echo %TARGET%\manual\images\ModEditor.png
if exist "%TARGET%\manual\images\MousePreferences.png" del /f /q "%TARGET%\manual\images\MousePreferences.png" && echo %TARGET%\manual\images\MousePreferences.png
if exist "%TARGET%\manual\images\MoveBrushesToEntity.png" del /f /q "%TARGET%\manual\images\MoveBrushesToEntity.png" && echo %TARGET%\manual\images\MoveBrushesToEntity.png
if exist "%TARGET%\manual\images\MoveObjectsByOffset.png" del /f /q "%TARGET%\manual\images\MoveObjectsByOffset.png" && echo %TARGET%\manual\images\MoveObjectsByOffset.png
if exist "%TARGET%\manual\images\MoveTrace.png" del /f /q "%TARGET%\manual\images\MoveTrace.png" && echo %TARGET%\manual\images\MoveTrace.png
if exist "%TARGET%\manual\images\ObjectSelection.gif" del /f /q "%TARGET%\manual\images\ObjectSelection.gif" && echo %TARGET%\manual\images\ObjectSelection.gif
if exist "%TARGET%\manual\images\PastePositioning3D.gif" del /f /q "%TARGET%\manual\images\PastePositioning3D.gif" && echo %TARGET%\manual\images\PastePositioning3D.gif
if exist "%TARGET%\manual\images\ProtectedProperties.png" del /f /q "%TARGET%\manual\images\ProtectedProperties.png" && echo %TARGET%\manual\images\ProtectedProperties.png
if exist "%TARGET%\manual\images\ReplaceTexture.png" del /f /q "%TARGET%\manual\images\ReplaceTexture.png" && echo %TARGET%\manual\images\ReplaceTexture.png
if exist "%TARGET%\manual\images\RotateHandle2D.png" del /f /q "%TARGET%\manual\images\RotateHandle2D.png" && echo %TARGET%\manual\images\RotateHandle2D.png
if exist "%TARGET%\manual\images\RotateHandle3D.png" del /f /q "%TARGET%\manual\images\RotateHandle3D.png" && echo %TARGET%\manual\images\RotateHandle3D.png
if exist "%TARGET%\manual\images\RotateTool.gif" del /f /q "%TARGET%\manual\images\RotateTool.gif" && echo %TARGET%\manual\images\RotateTool.gif
if exist "%TARGET%\manual\images\RotateToolControls.png" del /f /q "%TARGET%\manual\images\RotateToolControls.png" && echo %TARGET%\manual\images\RotateToolControls.png
if exist "%TARGET%\manual\images\ScalableCylinderStretch.gif" del /f /q "%TARGET%\manual\images\ScalableCylinderStretch.gif" && echo %TARGET%\manual\images\ScalableCylinderStretch.gif
if exist "%TARGET%\manual\images\ScalableHollowCylinder.png" del /f /q "%TARGET%\manual\images\ScalableHollowCylinder.png" && echo %TARGET%\manual\images\ScalableHollowCylinder.png
if exist "%TARGET%\manual\images\Scale3DCorner.gif" del /f /q "%TARGET%\manual\images\Scale3DCorner.gif" && echo %TARGET%\manual\images\Scale3DCorner.gif
if exist "%TARGET%\manual\images\Scale3DEdge.gif" del /f /q "%TARGET%\manual\images\Scale3DEdge.gif" && echo %TARGET%\manual\images\Scale3DEdge.gif
if exist "%TARGET%\manual\images\Scale3DSide.gif" del /f /q "%TARGET%\manual\images\Scale3DSide.gif" && echo %TARGET%\manual\images\Scale3DSide.gif
if exist "%TARGET%\manual\images\Scale3DSideCenter.gif" del /f /q "%TARGET%\manual\images\Scale3DSideCenter.gif" && echo %TARGET%\manual\images\Scale3DSideCenter.gif
if exist "%TARGET%\manual\images\ScaleToolToolbar.png" del /f /q "%TARGET%\manual\images\ScaleToolToolbar.png" && echo %TARGET%\manual\images\ScaleToolToolbar.png
if exist "%TARGET%\manual\images\SelectTouching.gif" del /f /q "%TARGET%\manual\images\SelectTouching.gif" && echo %TARGET%\manual\images\SelectTouching.gif
if exist "%TARGET%\manual\images\SetDefaultPropertiesMenu.png" del /f /q "%TARGET%\manual\images\SetDefaultPropertiesMenu.png" && echo %TARGET%\manual\images\SetDefaultPropertiesMenu.png
if exist "%TARGET%\manual\images\Shear3DVertical.gif" del /f /q "%TARGET%\manual\images\Shear3DVertical.gif" && echo %TARGET%\manual\images\Shear3DVertical.gif
if exist "%TARGET%\manual\images\SmartChoiceEditor.png" del /f /q "%TARGET%\manual\images\SmartChoiceEditor.png" && echo %TARGET%\manual\images\SmartChoiceEditor.png
if exist "%TARGET%\manual\images\SmartColorEditor.png" del /f /q "%TARGET%\manual\images\SmartColorEditor.png" && echo %TARGET%\manual\images\SmartColorEditor.png
if exist "%TARGET%\manual\images\SmartSpawnflagsEditor.png" del /f /q "%TARGET%\manual\images\SmartSpawnflagsEditor.png" && echo %TARGET%\manual\images\SmartSpawnflagsEditor.png
if exist "%TARGET%\manual\images\SmartWadEditor.png" del /f /q "%TARGET%\manual\images\SmartWadEditor.png" && echo %TARGET%\manual\images\SmartWadEditor.png
if exist "%TARGET%\manual\images\SoftBounds.png" del /f /q "%TARGET%\manual\images\SoftBounds.png" && echo %TARGET%\manual\images\SoftBounds.png
if exist "%TARGET%\manual\images\SweepTool.gif" del /f /q "%TARGET%\manual\images\SweepTool.gif" && echo %TARGET%\manual\images\SweepTool.gif
if exist "%TARGET%\manual\images\TextureBrowser.png" del /f /q "%TARGET%\manual\images\TextureBrowser.png" && echo %TARGET%\manual\images\TextureBrowser.png
if exist "%TARGET%\manual\images\TextureCollectionEditor.png" del /f /q "%TARGET%\manual\images\TextureCollectionEditor.png" && echo %TARGET%\manual\images\TextureCollectionEditor.png
if exist "%TARGET%\manual\images\ToolbarTools.png" del /f /q "%TARGET%\manual\images\ToolbarTools.png" && echo %TARGET%\manual\images\ToolbarTools.png
if exist "%TARGET%\manual\images\UVEditor.png" del /f /q "%TARGET%\manual\images\UVEditor.png" && echo %TARGET%\manual\images\UVEditor.png
if exist "%TARGET%\manual\images\UVEditorToolbar.png" del /f /q "%TARGET%\manual\images\UVEditorToolbar.png" && echo %TARGET%\manual\images\UVEditorToolbar.png
if exist "%TARGET%\manual\images\UVLock.png" del /f /q "%TARGET%\manual\images\UVLock.png" && echo %TARGET%\manual\images\UVLock.png
if exist "%TARGET%\manual\images\UpdateAnglePropertyAfterTransform.png" del /f /q "%TARGET%\manual\images\UpdateAnglePropertyAfterTransform.png" && echo %TARGET%\manual\images\UpdateAnglePropertyAfterTransform.png
if exist "%TARGET%\manual\images\UpdateIndicator.png" del /f /q "%TARGET%\manual\images\UpdateIndicator.png" && echo %TARGET%\manual\images\UpdateIndicator.png
if exist "%TARGET%\manual\images\UpdatePreferences.png" del /f /q "%TARGET%\manual\images\UpdatePreferences.png" && echo %TARGET%\manual\images\UpdatePreferences.png
if exist "%TARGET%\manual\images\VertexToolFaceChopping.gif" del /f /q "%TARGET%\manual\images\VertexToolFaceChopping.gif" && echo %TARGET%\manual\images\VertexToolFaceChopping.gif
if exist "%TARGET%\manual\images\VertexToolGuide.png" del /f /q "%TARGET%\manual\images\VertexToolGuide.png" && echo %TARGET%\manual\images\VertexToolGuide.png
if exist "%TARGET%\manual\images\VertexToolHandles.png" del /f /q "%TARGET%\manual\images\VertexToolHandles.png" && echo %TARGET%\manual\images\VertexToolHandles.png
if exist "%TARGET%\manual\images\VertexToolSplitting.gif" del /f /q "%TARGET%\manual\images\VertexToolSplitting.gif" && echo %TARGET%\manual\images\VertexToolSplitting.gif
if exist "%TARGET%\manual\images\VertexToolVertexClumping.gif" del /f /q "%TARGET%\manual\images\VertexToolVertexClumping.gif" && echo %TARGET%\manual\images\VertexToolVertexClumping.gif
if exist "%TARGET%\manual\images\ViewDropdown.png" del /f /q "%TARGET%\manual\images\ViewDropdown.png" && echo %TARGET%\manual\images\ViewDropdown.png
if exist "%TARGET%\manual\images\ViewPreferences.png" del /f /q "%TARGET%\manual\images\ViewPreferences.png" && echo %TARGET%\manual\images\ViewPreferences.png
if exist "%TARGET%\manual\images\WelcomeWindow.png" del /f /q "%TARGET%\manual\images\WelcomeWindow.png" && echo %TARGET%\manual\images\WelcomeWindow.png
if exist "%TARGET%\manual\images\icon.png" del /f /q "%TARGET%\manual\images\icon.png" && echo %TARGET%\manual\images\icon.png
if exist "%TARGET%\manual\index.html" del /f /q "%TARGET%\manual\index.html" && echo %TARGET%\manual\index.html
if exist "%TARGET%\manual\shortcuts.js" del /f /q "%TARGET%\manual\shortcuts.js" && echo %TARGET%\manual\shortcuts.js
if exist "%TARGET%\manual\shortcuts_helper.js" del /f /q "%TARGET%\manual\shortcuts_helper.js" && echo %TARGET%\manual\shortcuts_helper.js
if exist "%TARGET%\manual\toc_toggler.js" del /f /q "%TARGET%\manual\toc_toggler.js" && echo %TARGET%\manual\toc_toggler.js
if exist "%TARGET%\miniz.dll" del /f /q "%TARGET%\miniz.dll" && echo %TARGET%\miniz.dll
if exist "%TARGET%\minizip.dll" del /f /q "%TARGET%\minizip.dll" && echo %TARGET%\minizip.dll
if exist "%TARGET%\openjp2.dll" del /f /q "%TARGET%\openjp2.dll" && echo %TARGET%\openjp2.dll
if exist "%TARGET%\platforms\qwindows.dll" del /f /q "%TARGET%\platforms\qwindows.dll" && echo %TARGET%\platforms\qwindows.dll
if exist "%TARGET%\poly2tri.dll" del /f /q "%TARGET%\poly2tri.dll" && echo %TARGET%\poly2tri.dll
if exist "%TARGET%\pugixml.dll" del /f /q "%TARGET%\pugixml.dll" && echo %TARGET%\pugixml.dll
if exist "%TARGET%\raw.dll" del /f /q "%TARGET%\raw.dll" && echo %TARGET%\raw.dll
if exist "%TARGET%\shader\ClipHandle.vertsh" del /f /q "%TARGET%\shader\ClipHandle.vertsh" && echo %TARGET%\shader\ClipHandle.vertsh
if exist "%TARGET%\shader\ColoredHandle.vertsh" del /f /q "%TARGET%\shader\ColoredHandle.vertsh" && echo %TARGET%\shader\ColoredHandle.vertsh
if exist "%TARGET%\shader\ColoredText.vertsh" del /f /q "%TARGET%\shader\ColoredText.vertsh" && echo %TARGET%\shader\ColoredText.vertsh
if exist "%TARGET%\shader\Compass.fragsh" del /f /q "%TARGET%\shader\Compass.fragsh" && echo %TARGET%\shader\Compass.fragsh
if exist "%TARGET%\shader\Compass.vertsh" del /f /q "%TARGET%\shader\Compass.vertsh" && echo %TARGET%\shader\Compass.vertsh
if exist "%TARGET%\shader\CompassOutline.vertsh" del /f /q "%TARGET%\shader\CompassOutline.vertsh" && echo %TARGET%\shader\CompassOutline.vertsh
if exist "%TARGET%\shader\Edge.fragsh" del /f /q "%TARGET%\shader\Edge.fragsh" && echo %TARGET%\shader\Edge.fragsh
if exist "%TARGET%\shader\Edge.vertsh" del /f /q "%TARGET%\shader\Edge.vertsh" && echo %TARGET%\shader\Edge.vertsh
if exist "%TARGET%\shader\EntityModel.fragsh" del /f /q "%TARGET%\shader\EntityModel.fragsh" && echo %TARGET%\shader\EntityModel.fragsh
if exist "%TARGET%\shader\EntityModel.vertsh" del /f /q "%TARGET%\shader\EntityModel.vertsh" && echo %TARGET%\shader\EntityModel.vertsh
if exist "%TARGET%\shader\Face.fragsh" del /f /q "%TARGET%\shader\Face.fragsh" && echo %TARGET%\shader\Face.fragsh
if exist "%TARGET%\shader\Face.vertsh" del /f /q "%TARGET%\shader\Face.vertsh" && echo %TARGET%\shader\Face.vertsh
if exist "%TARGET%\shader\Grid.fragsh" del /f /q "%TARGET%\shader\Grid.fragsh" && echo %TARGET%\shader\Grid.fragsh
if exist "%TARGET%\shader\Grid2D.fragsh" del /f /q "%TARGET%\shader\Grid2D.fragsh" && echo %TARGET%\shader\Grid2D.fragsh
if exist "%TARGET%\shader\Grid2D.vertsh" del /f /q "%TARGET%\shader\Grid2D.vertsh" && echo %TARGET%\shader\Grid2D.vertsh
if exist "%TARGET%\shader\GridCommon.fragsh" del /f /q "%TARGET%\shader\GridCommon.fragsh" && echo %TARGET%\shader\GridCommon.fragsh
if exist "%TARGET%\shader\Handle.fragsh" del /f /q "%TARGET%\shader\Handle.fragsh" && echo %TARGET%\shader\Handle.fragsh
if exist "%TARGET%\shader\Handle.vertsh" del /f /q "%TARGET%\shader\Handle.vertsh" && echo %TARGET%\shader\Handle.vertsh
if exist "%TARGET%\shader\LinkArrow.fragsh" del /f /q "%TARGET%\shader\LinkArrow.fragsh" && echo %TARGET%\shader\LinkArrow.fragsh
if exist "%TARGET%\shader\LinkArrow.vertsh" del /f /q "%TARGET%\shader\LinkArrow.vertsh" && echo %TARGET%\shader\LinkArrow.vertsh
if exist "%TARGET%\shader\LinkLine.fragsh" del /f /q "%TARGET%\shader\LinkLine.fragsh" && echo %TARGET%\shader\LinkLine.fragsh
if exist "%TARGET%\shader\LinkLine.vertsh" del /f /q "%TARGET%\shader\LinkLine.vertsh" && echo %TARGET%\shader\LinkLine.vertsh
if exist "%TARGET%\shader\MapBounds.fragsh" del /f /q "%TARGET%\shader\MapBounds.fragsh" && echo %TARGET%\shader\MapBounds.fragsh
if exist "%TARGET%\shader\MaterialBrowser.fragsh" del /f /q "%TARGET%\shader\MaterialBrowser.fragsh" && echo %TARGET%\shader\MaterialBrowser.fragsh
if exist "%TARGET%\shader\MaterialBrowser.vertsh" del /f /q "%TARGET%\shader\MaterialBrowser.vertsh" && echo %TARGET%\shader\MaterialBrowser.vertsh
if exist "%TARGET%\shader\MaterialBrowserBorder.fragsh" del /f /q "%TARGET%\shader\MaterialBrowserBorder.fragsh" && echo %TARGET%\shader\MaterialBrowserBorder.fragsh
if exist "%TARGET%\shader\MaterialBrowserBorder.vertsh" del /f /q "%TARGET%\shader\MaterialBrowserBorder.vertsh" && echo %TARGET%\shader\MaterialBrowserBorder.vertsh
if exist "%TARGET%\shader\MiniMapEdge.fragsh" del /f /q "%TARGET%\shader\MiniMapEdge.fragsh" && echo %TARGET%\shader\MiniMapEdge.fragsh
if exist "%TARGET%\shader\MiniMapEdge.vertsh" del /f /q "%TARGET%\shader\MiniMapEdge.vertsh" && echo %TARGET%\shader\MiniMapEdge.vertsh
if exist "%TARGET%\shader\Text.fragsh" del /f /q "%TARGET%\shader\Text.fragsh" && echo %TARGET%\shader\Text.fragsh
if exist "%TARGET%\shader\Text.vertsh" del /f /q "%TARGET%\shader\Text.vertsh" && echo %TARGET%\shader\Text.vertsh
if exist "%TARGET%\shader\TextBackground.fragsh" del /f /q "%TARGET%\shader\TextBackground.fragsh" && echo %TARGET%\shader\TextBackground.fragsh
if exist "%TARGET%\shader\TextBackground.vertsh" del /f /q "%TARGET%\shader\TextBackground.vertsh" && echo %TARGET%\shader\TextBackground.vertsh
if exist "%TARGET%\shader\Triangle.fragsh" del /f /q "%TARGET%\shader\Triangle.fragsh" && echo %TARGET%\shader\Triangle.fragsh
if exist "%TARGET%\shader\Triangle.vertsh" del /f /q "%TARGET%\shader\Triangle.vertsh" && echo %TARGET%\shader\Triangle.vertsh
if exist "%TARGET%\shader\UVView.fragsh" del /f /q "%TARGET%\shader\UVView.fragsh" && echo %TARGET%\shader\UVView.fragsh
if exist "%TARGET%\shader\UVView.vertsh" del /f /q "%TARGET%\shader\UVView.vertsh" && echo %TARGET%\shader\UVView.vertsh
if exist "%TARGET%\shader\VaryingPC.fragsh" del /f /q "%TARGET%\shader\VaryingPC.fragsh" && echo %TARGET%\shader\VaryingPC.fragsh
if exist "%TARGET%\shader\VaryingPC.vertsh" del /f /q "%TARGET%\shader\VaryingPC.vertsh" && echo %TARGET%\shader\VaryingPC.vertsh
if exist "%TARGET%\shader\VaryingPUniformC.vertsh" del /f /q "%TARGET%\shader\VaryingPUniformC.vertsh" && echo %TARGET%\shader\VaryingPUniformC.vertsh
if exist "%TARGET%\styles\qmodernwindowsstyle.dll" del /f /q "%TARGET%\styles\qmodernwindowsstyle.dll" && echo %TARGET%\styles\qmodernwindowsstyle.dll
if exist "%TARGET%\stylesheets\base.qss" del /f /q "%TARGET%\stylesheets\base.qss" && echo %TARGET%\stylesheets\base.qss
if exist "%TARGET%\tiff.dll" del /f /q "%TARGET%\tiff.dll" && echo %TARGET%\tiff.dll
if exist "%TARGET%\tinyxml2.dll" del /f /q "%TARGET%\tinyxml2.dll" && echo %TARGET%\tinyxml2.dll
if exist "%TARGET%\tls\qcertonlybackend.dll" del /f /q "%TARGET%\tls\qcertonlybackend.dll" && echo %TARGET%\tls\qcertonlybackend.dll
if exist "%TARGET%\tls\qschannelbackend.dll" del /f /q "%TARGET%\tls\qschannelbackend.dll" && echo %TARGET%\tls\qschannelbackend.dll
if exist "%TARGET%\update\install_update.bat" del /f /q "%TARGET%\update\install_update.bat" && echo %TARGET%\update\install_update.bat
if exist "%TARGET%\zlib1.dll" del /f /q "%TARGET%\zlib1.dll" && echo %TARGET%\zlib1.dll
rd "%TARGET%\update" 2>nul
rd "%TARGET%\tls" 2>nul
rd "%TARGET%\stylesheets" 2>nul
rd "%TARGET%\styles" 2>nul
rd "%TARGET%\shader" 2>nul
rd "%TARGET%\platforms" 2>nul
rd "%TARGET%\manual\images" 2>nul
rd "%TARGET%\manual" 2>nul
rd "%TARGET%\images" 2>nul
rd "%TARGET%\games\Wrath" 2>nul
rd "%TARGET%\games\SoF\assets\textures" 2>nul
rd "%TARGET%\games\SoF\assets" 2>nul
rd "%TARGET%\games\SoF" 2>nul
rd "%TARGET%\games\Quetoo" 2>nul
rd "%TARGET%\games\Quake3" 2>nul
rd "%TARGET%\games\Quake2" 2>nul
rd "%TARGET%\games\Quake" 2>nul
rd "%TARGET%\games\Neverball\assets" 2>nul
rd "%TARGET%\games\Neverball" 2>nul
rd "%TARGET%\games\Kingpin" 2>nul
rd "%TARGET%\games\Hexen2" 2>nul
rd "%TARGET%\games\Heretic2" 2>nul
rd "%TARGET%\games\Halflife" 2>nul
rd "%TARGET%\games\Generic" 2>nul
rd "%TARGET%\games\DigitalPaintball2" 2>nul
rd "%TARGET%\games\Daikatana" 2>nul
rd "%TARGET%\games\DDayNormandy" 2>nul
rd "%TARGET%\games" 2>nul
rd "%TARGET%\fonts" 2>nul
rd "%TARGET%\defaults\assets\textures" 2>nul
rd "%TARGET%\defaults\assets" 2>nul
rd "%TARGET%\defaults" 2>nul
rem TB_DELETE_INSTALLED_FILES_END
echo.

:: Copy the contents of the replacement folder to the target
echo Installing updated files from "%REPLACEMENT%" to "%TARGET%"...
xcopy "%REPLACEMENT%\*" "%TARGET%\" /e /h /y
echo.

echo Deleting "%REPLACEMENT%"...
rd /s /q "%REPLACEMENT%"
echo.

echo Operation completed successfully.

if "%APP%" neq "" (
  :: Check if the app exists
  if not exist "%APP%" (
      echo Error: Application "%APP%" does not exist.
      exit /b 1
  )

  :: Start the app
  echo Starting the application...
  start "" "%APP%"
)
exit /b 0
