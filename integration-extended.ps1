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
      $body = $reader.ReadToEnd(); $reader.Close()
    }
    [PSCustomObject]@{ ok=$false; status=$status; body=$body }
  }
}

$results=@()
$results += [PSCustomObject]@{step='reset';res=(CallApi 'POST' '/api/admin/reset' @{confirm='RESET_ALL_DATA'})}
$results += [PSCustomObject]@{step='register-parent';res=(CallApi 'POST' '/api/auth/register-parent' @{username='parent1';password='parent123';securityQuestion='q';securityAnswer='a'})}
$results += [PSCustomObject]@{step='create-kid1';res=(CallApi 'POST' '/api/auth/kids' @{username='kid1';password='kid123';securityQuestion='q';securityAnswer='a'})}
$results += [PSCustomObject]@{step='create-kid2';res=(CallApi 'POST' '/api/auth/kids' @{username='kid2';password='kid234';securityQuestion='q';securityAnswer='a'})}

# concurrency lock checks
$results += [PSCustomObject]@{step='lock-parent-deposit';res=(CallApi 'POST' '/api/device/lock' @{action='acquire';username='parent1';mode='deposit';ttlSeconds=120})}
$results += [PSCustomObject]@{step='lock-kid1-deposit-conflict';res=(CallApi 'POST' '/api/device/lock' @{action='acquire';username='kid1';mode='deposit';ttlSeconds=120})}
$results += [PSCustomObject]@{step='lock-release-parent';res=(CallApi 'POST' '/api/device/lock' @{action='release';username='parent1'})}

# seed machine with 5x coin20 and kid1 balance 100
1..5 | ForEach-Object {
  $results += [PSCustomObject]@{step="deposit-coin20-$_";res=(CallApi 'POST' '/api/deposit' @{amount=20;source='coin';account='kid1';role='kid';note='seed coin'})}
}
$results += [PSCustomObject]@{step='inventory-after-seed';res=(CallApi 'GET' '/api/inventory' $null)}
$results += [PSCustomObject]@{step='balance-kid1-after-seed';res=(CallApi 'GET' '/api/accounts/balance?username=kid1' $null)}

# withdraw conflict check
$results += [PSCustomObject]@{step='lock-kid1-withdraw';res=(CallApi 'POST' '/api/device/lock' @{action='acquire';username='kid1';mode='withdraw';ttlSeconds=120})}
$results += [PSCustomObject]@{step='lock-kid2-withdraw-conflict';res=(CallApi 'POST' '/api/device/lock' @{action='acquire';username='kid2';mode='withdraw';ttlSeconds=120})}

# withdraw 100 should fallback to 5x coin20
$results += [PSCustomObject]@{step='withdraw-100-kid1';res=(CallApi 'POST' '/api/command' @{command='WITHDRAW 100';account='kid1';lockOwner='kid1';role='kid';note='fallback test'})}
$results += [PSCustomObject]@{step='inventory-after-withdraw-100';res=(CallApi 'GET' '/api/inventory' $null)}
$results += [PSCustomObject]@{step='balance-kid1-after-withdraw-100';res=(CallApi 'GET' '/api/accounts/balance?username=kid1' $null)}
$results += [PSCustomObject]@{step='lock-release-kid1';res=(CallApi 'POST' '/api/device/lock' @{action='release';username='kid1'})}

# parent self deposit (wallet)
$results += [PSCustomObject]@{step='parent-self-deposit';res=(CallApi 'POST' '/api/accounts/deposit' @{username='parent1';role='parent';amount=55;note='parent wallet topup'})}
$results += [PSCustomObject]@{step='balance-parent1';res=(CallApi 'GET' '/api/accounts/balance?username=parent1' $null)}

# pending withdrawal flow persisted in db
$results += [PSCustomObject]@{step='pending-create-kid2';res=(CallApi 'POST' '/api/pending-withdrawals' @{child='kid2';amount=40;note='snacks'})}
$results += [PSCustomObject]@{step='pending-list';res=(CallApi 'GET' '/api/pending-withdrawals' $null)}

# transaction history integrity
$results += [PSCustomObject]@{step='tx-kid1';res=(CallApi 'GET' '/api/accounts/transactions?username=kid1' $null)}
$results += [PSCustomObject]@{step='tx-parent1';res=(CallApi 'GET' '/api/accounts/transactions?username=parent1' $null)}

$results | ForEach-Object { "STEP=$($_.step) STATUS=$($_.res.status) OK=$($_.res.ok) BODY=$($_.res.body)" }
