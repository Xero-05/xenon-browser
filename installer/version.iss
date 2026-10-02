// Strict semantic version comparison; numeric prerelease identifiers are not
// compared lexically (alpha.10 is newer than alpha.9). Build metadata is omitted.
function NextIdentifier(var Remaining: String): String;
var P: Integer;
begin
  P := Pos('.', Remaining);
  if P = 0 then begin Result := Remaining; Remaining := ''; end
  else begin Result := Copy(Remaining, 1, P - 1); Delete(Remaining, 1, P); end;
end;

function DecimalIdentifier(S: String): Boolean;
var I: Integer;
begin
  Result := Length(S) > 0;
  for I := 1 to Length(S) do
    if (S[I] < '0') or (S[I] > '9') then Result := False;
end;

function ValidRelease(S: String): Boolean;
var Core, Pre, Part: String; P, I, N: Integer;
begin
  Result := False;
  if (Length(S) = 0) or (Length(S) > 128) then Exit;
  P := Pos('-', S); Core := S; Pre := '';
  if P > 0 then begin Core := Copy(S, 1, P - 1); Pre := Copy(S, P + 1, Length(S)); if Pre = '' then Exit; end;
  if (Core = '') or (Core[Length(Core)] = '.') then Exit;
  for N := 1 to 3 do begin
    Part := NextIdentifier(Core);
    if not DecimalIdentifier(Part) then Exit;
    if (Length(Part) > 1) and (Part[1] = '0') then Exit;
    if (N < 3) and (Core = '') then Exit;
  end;
  if Core <> '' then Exit;
  if (Length(S) > 0) and (S[Length(S)] = '.') then Exit;
  while Pre <> '' do begin
    Part := NextIdentifier(Pre); if Part = '' then Exit;
    for I := 1 to Length(Part) do
      if not (((Part[I] >= '0') and (Part[I] <= '9')) or
              ((Part[I] >= 'a') and (Part[I] <= 'z')) or
              ((Part[I] >= 'A') and (Part[I] <= 'Z')) or (Part[I] = '-')) then Exit;
    if DecimalIdentifier(Part) and (Length(Part) > 1) and (Part[1] = '0') then Exit;
  end;
  Result := True;
end;

function CompareIdentifier(A, B: String): Integer;
begin
  if DecimalIdentifier(A) and DecimalIdentifier(B) then begin
    Result := Length(A) - Length(B);
    if Result = 0 then Result := CompareStr(A, B);
  end else if DecimalIdentifier(A) then Result := -1
  else if DecimalIdentifier(B) then Result := 1
  else Result := CompareStr(A, B);
end;

function CompareRelease(A, B: String): Integer;
var ACore, BCore, APre, BPre, AP, BP: String; P, I: Integer;
begin
  ACore := A; BCore := B; APre := ''; BPre := '';
  P := Pos('-', A); if P > 0 then begin ACore := Copy(A, 1, P - 1); APre := Copy(A, P + 1, Length(A)); end;
  P := Pos('-', B); if P > 0 then begin BCore := Copy(B, 1, P - 1); BPre := Copy(B, P + 1, Length(B)); end;
  for I := 1 to 3 do begin
    AP := NextIdentifier(ACore); BP := NextIdentifier(BCore);
    Result := CompareIdentifier(AP, BP); if Result <> 0 then Exit;
  end;
  Result := 0; if (APre = '') and (BPre = '') then Exit;
  if APre = '' then begin Result := 1; Exit; end;
  if BPre = '' then begin Result := -1; Exit; end;
  while (APre <> '') and (BPre <> '') do begin
    AP := NextIdentifier(APre); BP := NextIdentifier(BPre);
    Result := CompareIdentifier(AP, BP); if Result <> 0 then Exit;
  end;
  if APre <> '' then Result := 1 else if BPre <> '' then Result := -1;
end;
