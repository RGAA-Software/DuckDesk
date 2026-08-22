param(
    [int]$LifetimeSeconds = 600
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$context = New-Object System.Windows.Forms.ApplicationContext
$forms = New-Object System.Collections.Generic.List[System.Windows.Forms.Form]
$labels = New-Object System.Collections.Generic.List[System.Windows.Forms.Label]

function Add-StimulusForms {
    $screens = [System.Windows.Forms.Screen]::AllScreens
    while ($script:forms.Count -lt $screens.Count) {
        $screenIndex = $script:forms.Count
        $screen = $screens[$screenIndex]
        $form = New-Object System.Windows.Forms.Form
        $form.Text = "GammaRay capture stimulus - screen $screenIndex"
        $form.StartPosition = [System.Windows.Forms.FormStartPosition]::Manual
        $form.Location = New-Object System.Drawing.Point(($screen.Bounds.Left + 80), ($screen.Bounds.Top + 80))
        $form.Size = New-Object System.Drawing.Size(420, 160)
        $form.TopMost = $true
        $form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::FixedToolWindow

        $label = New-Object System.Windows.Forms.Label
        $label.Dock = [System.Windows.Forms.DockStyle]::Fill
        $label.TextAlign = [System.Drawing.ContentAlignment]::MiddleCenter
        $label.Font = New-Object System.Drawing.Font('Segoe UI', 18, [System.Drawing.FontStyle]::Bold)
        $label.ForeColor = [System.Drawing.Color]::White
        $label.Text = "GammaRay E2E screen $screenIndex"
        $form.Controls.Add($label)
        $form.Show()

        $script:forms.Add($form)
        $script:labels.Add($label)
    }
}

Add-StimulusForms

$started = [DateTime]::UtcNow
$tick = 0
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 100
$timer.Add_Tick({
    Add-StimulusForms
    $script:tick++
    for ($i = 0; $i -lt $script:forms.Count; $i++) {
        $hue = ($script:tick * 11 + $i * 97) % 360
        $sector = [Math]::Floor($hue / 60)
        $phase = ($hue % 60) / 60.0
        $x = [int](255 * (1.0 - [Math]::Abs(($phase * 2.0) - 1.0)))
        $color = switch ($sector) {
            0 { [Drawing.Color]::FromArgb(255, $x, 32) }
            1 { [Drawing.Color]::FromArgb($x, 255, 32) }
            2 { [Drawing.Color]::FromArgb(32, 255, $x) }
            3 { [Drawing.Color]::FromArgb(32, $x, 255) }
            4 { [Drawing.Color]::FromArgb($x, 32, 255) }
            default { [Drawing.Color]::FromArgb(255, 32, $x) }
        }
        $script:forms[$i].BackColor = $color
        $script:labels[$i].BackColor = $color
        $script:labels[$i].Text = "GammaRay E2E screen $i`n$([DateTime]::Now.ToString('HH:mm:ss.fff'))"
    }
    if (([DateTime]::UtcNow - $script:started).TotalSeconds -ge $LifetimeSeconds) {
        $timer.Stop()
        foreach ($form in $script:forms) { $form.Close() }
        $script:context.ExitThread()
    }
})
$timer.Start()
[System.Windows.Forms.Application]::Run($context)
