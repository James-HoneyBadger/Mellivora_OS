#!/usr/bin/perl
# csv.pl - CSV parser and column extractor for Mellivora OS
# Usage: Parses hardcoded CSV data; demonstrates split, array ops
# Run:   csv.pl

# Sample CSV data (embedded)
my @data = (
    "Name,Age,City,Score",
    "Alice,30,London,95",
    "Bob,25,Paris,87",
    "Charlie,35,Berlin,92",
    "Diana,28,Rome,88",
    "Eve,31,Madrid,79",
);

print "=== CSV Parser Demo ===\n\n";

# Parse and display all columns
print "Full table:\n";
print "-" x 40 . "\n";
foreach my $line (@data) {
    my @fields = split(/,/, $line);
    my $formatted = "";
    foreach my $f (@fields) {
        $formatted .= sprintf("%-12s", $f);
    }
    print $formatted . "\n";
}
print "\n";

# Extract just Name and Score columns (0 and 3)
print "Name and Score only:\n";
print "-" x 26 . "\n";
my $header = 1;
foreach my $line (@data) {
    my @fields = split(/,/, $line);
    if ($header) {
        print sprintf("%-14s %s\n", $fields[0], $fields[3]);
        $header = 0;
    } else {
        print sprintf("%-14s %s\n", $fields[0], $fields[3]);
    }
}
print "\n";

# Filter rows where Score > 90
print "High scorers (>90):\n";
print "-" x 26 . "\n";
my $first = 1;
foreach my $line (@data) {
    my @fields = split(/,/, $line);
    if ($first) { $first = 0; next; }   # skip header
    if ($fields[3] > 90) {
        print "$fields[0] scored $fields[3]\n";
    }
}
print "\n";

# Sort by score descending (simple bubble sort on array of lines)
my @rows = @data[1..$#data];  # skip header
my $n = scalar @rows;
for my $i (0 .. $n - 2) {
    for my $j (0 .. $n - 2 - $i) {
        my @a = split(/,/, $rows[$j]);
        my @b = split(/,/, $rows[$j+1]);
        if ($a[3] < $b[3]) {
            my $tmp = $rows[$j];
            $rows[$j] = $rows[$j+1];
            $rows[$j+1] = $tmp;
        }
    }
}
print "Sorted by score (high to low):\n";
print "-" x 30 . "\n";
foreach my $line (@rows) {
    my @f = split(/,/, $line);
    print "$f[0]: $f[3]\n";
}
