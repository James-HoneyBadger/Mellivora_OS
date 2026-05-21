#!/usr/bin/perl
# regex_demo.pl - Regular expression examples for Mellivora OS
# Demonstrates matching, capture groups, substitution, and splitting
# Run: regex_demo.pl

print "=== Regex Demo for Mellivora OS ===\n\n";

# 1. Simple match
my $str = "The quick brown fox jumps over the lazy dog";
print "String: \"$str\"\n\n";

if ($str =~ /quick/) {
    print "1. Match /quick/        : found\n";
} else {
    print "1. Match /quick/        : not found\n";
}

if ($str =~ /\bfox\b/) {
    print "2. Match /\\bfox\\b/      : found (whole word)\n";
}

if ($str =~ /^The/) {
    print "3. Match /^The/         : string starts with 'The'\n";
}

if ($str =~ /dog$/) {
    print "4. Match /dog\$/         : string ends with 'dog'\n";
}

print "\n";

# 2. Capture groups
if ($str =~ /(\w+) (\w+) (\w+)/) {
    print "5. Capture first 3 words: '$1', '$2', '$3'\n";
}

my $date = "2026-05-21";
if ($date =~ /^(\d{4})-(\d{2})-(\d{2})$/) {
    print "6. Date parse: year=$1 month=$2 day=$3\n";
}
print "\n";

# 3. Substitution
my $msg = "Hello World, Hello Perl";
(my $new = $msg) =~ s/Hello/Hi/g;
print "7. s/Hello/Hi/g       : \"$new\"\n";

(my $clean = "  whitespace   ") =~ s/^\s+|\s+$//g;
print "8. Trim whitespace    : \"$clean\"\n";

(my $digs = "abc123def456") =~ s/[a-z]//g;
print "9. Remove letters     : \"$digs\"\n";
print "\n";

# 4. Split with regex
my $csv = "one,two,,four,five";
my @parts = split(/,/, $csv);
print "10. split /,/ on \"$csv\":\n";
for my $i (0 .. $#parts) {
    print "    [$i] = \"$parts[$i]\"\n";
}
print "\n";

my $path = "/usr/local/bin/perl";
my @dirs = split(/\//, $path);
shift @dirs;  # remove empty first element
print "11. split /\\// on \"$path\":\n";
print "    " . join(" -> ", @dirs) . "\n";
print "\n";

# 5. Grep (filter with regex)
my @words = ("apple", "apricot", "banana", "avocado", "cherry", "almond");
my @a_words = grep { /^a/ } @words;
print "12. Words starting with 'a': " . join(", ", @a_words) . "\n";

my @long = grep { length($_) > 6 } @words;
print "13. Words longer than 6 chars: " . join(", ", @long) . "\n";
print "\n";

# 6. Named captures
my $log = "2026-05-21 14:32:07 ERROR kernel panic at 0x0000deadbeef";
if ($log =~ /(?<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) (?<level>\w+) (?<msg>.+)/) {
    print "14. Named captures:\n";
    print "    timestamp = $+{ts}\n";
    print "    level     = $+{level}\n";
    print "    message   = $+{msg}\n";
}
