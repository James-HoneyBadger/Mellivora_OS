#!/usr/bin/perl
# csv_totals.pl - Parse records, validate fields, and aggregate totals

my @rows = (
    "editor,3",
    "games,5",
    "network,2",
    "editor,4",
);

my %totals;
for my $row (@rows) {
    my ($category, $count) = split /,/, $row, 2;
    next unless defined $category && defined $count && $count =~ /^\d+$/;
    $totals{$category} += $count;
}

for my $category (sort keys %totals) {
    print "$category: $totals{$category}\n";
}
