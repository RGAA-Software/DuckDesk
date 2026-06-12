;ÆÁÄ»¿í¶È
!define SM_CXSCREEN 0
;ÆÁÄ»¸ß¶È
!define SM_CYSCREEN 1
!define SWP_NOSIZE  0x0001

!macro _GetWindowLong Set_Long
  System::Call "user32::GetWindowLong(i ${Set_Long}, i -16) i .r0"
  Pop   $0
  IntOp $0 $0 | 0xC00000
  IntOp $0 $0 - 0xC00000
  System::Call "user32::SetWindowLong(i ${Set_Long}, i -16, i $0)"
  Pop  $0
  Push $0
  System::Call "user32::GetWindowLong(i ${Set_Long}, i -20) i .s"
  Pop  $0
  IntOp $0 $0 | 0x00000001
  IntOp $0 $0 - 0x00000001
  System::Call "user32::SetWindowLong(i ${Set_Long}, i -20, i $0)"
  GetDlgItem $SW_Hid $HWNDPARENT 1028
  ShowWindow $SW_Hid ${SW_HIDE}
	GetDlgItem $SW_Hid $HWNDPARENT 1034
  ShowWindow $SW_Hid ${SW_HIDE}
	GetDlgItem $SW_Hid $HWNDPARENT 1035
  ShowWindow $SW_Hid ${SW_HIDE}
	GetDlgItem $SW_Hid $HWNDPARENT 1036
  ShowWindow $SW_Hid ${SW_HIDE}
  GetDlgItem $SW_Hid $HWNDPARENT 1037
  ShowWindow $SW_Hid ${SW_HIDE}
  GetDlgItem $SW_Hid $HWNDPARENT 1038
  ShowWindow $SW_Hid ${SW_HIDE}
  GetDlgItem $SW_Hid $HWNDPARENT 1039
  ShowWindow $SW_Hid ${SW_HIDE}
  GetDlgItem $SW_Hid $HWNDPARENT 1256
  ShowWindow $SW_Hid ${SW_HIDE}
  SetCtlColors $HWNDPARENT ""  transparent
!macroend
!define GetWindowLong `!insertmacro _GetWindowLong`

!macro _CentWindow HWND CXSCREEN CYSCREEN
	System::Call `user32::GetSystemMetrics(i${SM_CXSCREEN})i.r1`
	System::Call `user32::GetSystemMetrics(i${SM_CYSCREEN})i.r2`
	IntOp $1 $1 - ${CXSCREEN}
	IntOp $2 $2 - ${CYSCREEN}
	IntOp $1 $1 / 2
	IntOp $2 $2 / 2
	System::Call `user32::SetWindowPos(i${HWND},i0,i$1,i$2,i${CXSCREEN},i${CYSCREEN},i0,i${SWP_NOSIZE})`
!macroend
!define CentWindow `!insertmacro _CentWindow`
