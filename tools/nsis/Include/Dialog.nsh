!ifndef __DIALOGBOX_NSH__
!define __DIALOGBOX_NSH__

!define IDI_HAND            32513
!define IDI_QUESTION        32514
!define IDI_EXCLAMATION     32515
!define IDI_INFORMATION     32516
!define IDI_ERROR           ${IDI_HAND}
!define IDI_WARNING         ${IDI_EXCLAMATION}
!define IDI_ASTERISK        ${IDI_INFORMATION}

!define DB_Init             "DialogBox::Init"
!define DB_AddHLine         "DialogBox::AddItem Static 0x54001010 0x00000020"
!define DB_AddVLine         "DialogBox::AddItem Static 0x54001011 0x00000020"
!define DB_AddLabel         "DialogBox::AddItem Static 0x54000100 0x00000020"
!define DB_AddIcon          "DialogBox::AddItem Static 0x54000103 0x00000000"
!define DB_AddBitmap        "DialogBox::AddItem Static 0x5400010E 0x00000000"
!define DB_AddButton        "DialogBox::AddItem Button 0x54010000 0x00000000"
!define DB_AddGroupBox      "DialogBox::AddItem Button 0x54000007 0x00000020"
!define DB_AddCheckBox      "DialogBox::AddItem Button 0x54012C03 0x00000000"
!define DB_AddRadioButton   "DialogBox::AddItem Button 0x54012C09 0x00000000"
!define DB_AddLink          "DialogBox::AddItem Link 0x54010000 0x00000000"
!define DB_AddText          "DialogBox::AddItem Edit 0x54010080 0x00000300"
!define DB_AddPassword      "DialogBox::AddItem Edit 0x540100A0 0x00000300"
!define DB_AddNumber        "DialogBox::AddItem Edit 0x54012080 0x00000300"
!define DB_AddComboBox      "DialogBox::AddItem ComboBox 0x56210242 0x00000300"
!define DB_AddDropList      "DialogBox::AddItem ComboBox 0x56210243 0x00000300"
!define DB_AddListBox       "DialogBox::AddItem ListBox 0x54211141 0x00000300"
!define DB_AddProgress      "DialogBox::AddItem msctls_progress32 0x54000000 0x00000300"
!define DB_Show             "DialogBox::Show"

!macro __DialogBox_CtlEvent __event __id __func
    Push "${__id}"
    Exch $R0
    Push $R1
    GetFunctionAddress $R1 ${__func}
    DialogBox::${__event} $R0 $R1
    Pop $R1
    Pop $R0
!macroend
!macro __DialogBox_DlgEvent __event __func
    Push $R0
    GetFunctionAddress $R0 ${__func}
    DialogBox::${__event} $R0
    Pop $R0
!macroend

!define DB_OnClick          "!insertmacro __DialogBox_CtlEvent OnClick"
!define DB_OnChange         "!insertmacro __DialogBox_CtlEvent OnChange"
!define DB_OnNotify         "!insertmacro __DialogBox_CtlEvent OnNotify"
!define DB_OnInit           "!insertmacro __DialogBox_DlgEvent OnInit"
!define DB_OnClose          "!insertmacro __DialogBox_DlgEvent OnClose"

!endif