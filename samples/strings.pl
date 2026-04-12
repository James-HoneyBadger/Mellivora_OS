#!/usr/bin/perl
# strings.pl - String operations demo for Mellivora OS

my $str = "Hello, Mellivora!";

print "Original: $str\n";
print "Length:   " . length($str) . "\n";
print "Upper:    " . uc($str) . "\n";
print "Lower:    " . lc($str) . "\n";
print "Substr:   " . substr($str, 7, 9) . "\n";
print "Index:    " . index($str, "Mellivora") . "\n";
print "Reverse:  " . reverse($str) . "\n";

my $a = "foo";
my $b = "bar";
print "Concat:   " . $a . $b . "\n";
print "Repeat:   " . $a x 3 . "\n";

print "Chr(65):  " . chr(65) . "\n";
print "Ord('A'): " . ord("A") . "\n";
