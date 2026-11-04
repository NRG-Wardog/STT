Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$form=[Windows.Forms.Form]::new()
$form.Text='HebEN controlled test editor'
$form.Size=[Drawing.Size]::new(650,330)
$form.StartPosition='CenterScreen'
$first=[Windows.Forms.TextBox]::new(); $first.AccessibleName='HebEN test field one'; $first.Location=[Drawing.Point]::new(30,30); $first.Width=560
$second=[Windows.Forms.TextBox]::new(); $second.AccessibleName='HebEN test field two'; $second.Location=[Drawing.Point]::new(30,90); $second.Width=560
$password=[Windows.Forms.TextBox]::new(); $password.AccessibleName='HebEN test password'; $password.Location=[Drawing.Point]::new(30,150); $password.Width=560; $password.UseSystemPasswordChar=$true
$form.Controls.AddRange(@($first,$second,$password))
$form.Add_Shown({$first.Focus() | Out-Null})
[Windows.Forms.Application]::Run($form)
