$base='http://localhost:3001'
function CallApi($method,$path,$payload){
  $url = "$base$path"
  $json = if($null -ne $payload){ $payload | ConvertTo-Json -Depth 10 -Compress } else { $null }
  try {
    if($null -ne $json){
      $resp = Invoke-WebRequest -UseBasicParsing -Method $method -Uri $url -ContentType 'application/json' -Body $json
    } else {
      $resp = Invoke-WebRequest -UseBasicParsing -Method $method -Uri $url
    }
    [PSCustomObject]@{ ok=$true; status=[int]$resp.StatusCode; body=$resp.Content }
  } catch {
    $status = -1; $body=''
    if($_.Exception.Response){
      $status = [int]$_.Exception.Response.StatusCode
      $reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
      $body = $reader.ReadToEnd()
      $reader.Close()
    }
    [PSCustomObject]@{ ok=$false; status=$status; body=$body }
  }
}

$results = @()
$results += [PSCustomObject]@{step='reset'; res=(CallApi 'POST' '/api/admin/reset' @{confirm='RESET_ALL_DATA'})}
$results += [PSCustomObject]@{step='register-parent'; res=(CallApi 'POST' '/api/auth/register-parent' @{username='parent1'; password='parent123'; securityQuestion='q'; securityAnswer='a'})}
$results += [PSCustomObject]@{step='login-parent'; res=(CallApi 'POST' '/api/auth/login' @{username='parent1'; password='parent123'; role='parent'})}
$results += [PSCustomObject]@{step='create-kid'; res=(CallApi 'POST' '/api/auth/kids' @{username='kid1'; password='kid123'; securityQuestion='q'; securityAnswer='a'})}
$results += [PSCustomObject]@{step='list-kids'; res=(CallApi 'GET' '/api/auth/kids' $null)}
$results += [PSCustomObject]@{step='lock-parent'; res=(CallApi 'POST' '/api/device/lock' @{action='acquire'; username='parent1'; mode='deposit'; ttlSeconds=120})}
$results += [PSCustomObject]@{step='lock-kid-conflict'; res=(CallApi 'POST' '/api/device/lock' @{action='acquire'; username='kid1'; mode='withdraw'; ttlSeconds=120})}
$results += [PSCustomObject]@{step='lock-release-parent'; res=(CallApi 'POST' '/api/device/lock' @{action='release'; username='parent1'})}
$results += [PSCustomObject]@{step='hardware-deposit-coin20'; res=(CallApi 'POST' '/api/deposit' @{amount=20; source='coin'; account='kid1'; role='kid'; note='test deposit'})}
$results += [PSCustomObject]@{step='inventory-after-deposit'; res=(CallApi 'GET' '/api/inventory' $null)}
$results += [PSCustomObject]@{step='balance-kid-after-deposit'; res=(CallApi 'GET' '/api/accounts/balance?username=kid1' $null)}
$results += [PSCustomObject]@{step='withdraw-no-lock-should-fail'; res=(CallApi 'POST' '/api/command' @{command='WITHDRAW 20'; account='kid1'; lockOwner='kid1'; role='kid'; note='test wd'})}
$results += [PSCustomObject]@{step='lock-kid'; res=(CallApi 'POST' '/api/device/lock' @{action='acquire'; username='kid1'; mode='withdraw'; ttlSeconds=120})}
$results += [PSCustomObject]@{step='withdraw-with-lock'; res=(CallApi 'POST' '/api/command' @{command='WITHDRAW 20'; account='kid1'; lockOwner='kid1'; role='kid'; note='test wd'})}
$results += [PSCustomObject]@{step='lock-release-kid'; res=(CallApi 'POST' '/api/device/lock' @{action='release'; username='kid1'})}
$results += [PSCustomObject]@{step='balance-kid-after-withdraw'; res=(CallApi 'GET' '/api/accounts/balance?username=kid1' $null)}
$results += [PSCustomObject]@{step='tx-kid'; res=(CallApi 'GET' '/api/accounts/transactions?username=kid1' $null)}
$results += [PSCustomObject]@{step='pending-create'; res=(CallApi 'POST' '/api/pending-withdrawals' @{child='kid1'; amount=40; note='snack'})}
$results += [PSCustomObject]@{step='pending-list'; res=(CallApi 'GET' '/api/pending-withdrawals' $null)}

$results | ForEach-Object { "STEP=$($_.step) STATUS=$($_.res.status) OK=$($_.res.ok) BODY=$($_.res.body)" }
