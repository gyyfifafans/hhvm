<?hh
<<__EntryPoint>>
function main_entry(): void {
  // TODO: Add more encodings
  ini_set('include_path', dirname(__FILE__));
  include_once('common.inc');

  // EUC-JP
  $euc_jp = '0123この文字列は日本語です。EUC-JPを使っています。日本語は面倒臭い。';

  print  mb_strcut($euc_jp,  6,   5,'EUC-JP') . "\n";
  print  mb_strcut($euc_jp,  0, 100,'EUC-JP') . "\n";

  $str = mb_strcut($euc_jp, 100, 10,'EUC-JP');
  ($str === false) ? print "OK\n" : print "NG: $str\n";

  $str = mb_strcut($euc_jp, -100, 10,'EUC-JP');
  ($str !== "") ?	print "OK: $str\n" : print "NG:\n";
}
