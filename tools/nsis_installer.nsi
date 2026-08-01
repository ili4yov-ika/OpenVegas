; NSIS Installer Script for OpenVegas
; Usage: makensis /DBUILD_DIR="path\to\build\install" nsis_installer.nsi

Unicode true
!include "MUI2.nsh"

; Product info
!define PRODUCT_NAME "OpenVegas"
!define PRODUCT_VERSION "0.1.0"
!define PRODUCT_PUBLISHER "OpenVegas contributors"
!define PRODUCT_WEB_SITE "https://github.com/OpenVegas/OpenVegas"

; Общие настройки
Name "${PRODUCT_NAME}"
OutFile "OpenVegas_Setup.exe"
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"
InstallDirRegKey HKLM "Software\${PRODUCT_NAME}" "InstallPath"
RequestExecutionLevel admin

; Интерфейс
!define MUI_ABORTWARNING

; Страницы
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Russian"
!insertmacro MUI_LANGUAGE "English"

; Секция установки
Section "OpenVegas" SEC_APP
  SectionIn RO

  SetOutPath "$INSTDIR"

  ; Копируем bin (exe и dll от windeployqt)
  File /r "${BUILD_DIR}\bin\*.*"

  ; Переводы (если установлены через CMake)
  SetOutPath "$INSTDIR\translations"
  File /nonfatal /r "${BUILD_DIR}\share\openvegas\translations\*.*"
  File /nonfatal /r "${BUILD_DIR}\share\OpenVegas\translations\*.*"

  WriteRegStr HKLM "Software\${PRODUCT_NAME}" "InstallPath" "$INSTDIR"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"

  CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\OpenVegas.exe"
  CreateShortCut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall ${PRODUCT_NAME}.lnk" "$INSTDIR\Uninstall.exe"

  ; Ассоциация .veg (открытие проектов Vegas / OpenVegas)
  WriteRegStr HKCR ".veg" "" "OpenVegas.Project"
  WriteRegStr HKCR "OpenVegas.Project" "" "OpenVegas Project"
  WriteRegStr HKCR "OpenVegas.Project\DefaultIcon" "" "$INSTDIR\OpenVegas.exe,0"
  WriteRegStr HKCR "OpenVegas.Project\shell\open\command" "" '"$INSTDIR\OpenVegas.exe" "%1"'
SectionEnd

Section "Uninstall"
  RMDir /r "$INSTDIR\translations"
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\iconengines"
  RMDir /r "$INSTDIR\generic"
  RMDir /r "$INSTDIR\tls"
  RMDir /r "$INSTDIR\networkinformation"
  Delete "$INSTDIR\OpenVegas.exe"
  Delete "$INSTDIR\Uninstall.exe"
  Delete "$INSTDIR\*.dll"
  RMDir /r "$INSTDIR"

  RMDir /r "$SMPROGRAMS\${PRODUCT_NAME}"

  DeleteRegKey HKCR "OpenVegas.Project"
  DeleteRegKey HKCR ".veg"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
  DeleteRegKey HKLM "Software\${PRODUCT_NAME}"
SectionEnd
