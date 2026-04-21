#!/usr/bin/perl
# arrays.pl - Array operations demo for Mellivora OS

my @nums = (10, 20, 30, 40, 50);

print "Array: ";
foreach my $n (@nums) {
    print "$n ";
}
print "\n";

print "Count: " . scalar(@nums) . "\n";

push @nums, 60;
print "After push 60: ";
foreach my $n (@nums) {
    print "$n ";
}
print "\n";

# Sum
my $sum = 0;
foreach my $n (@nums) {
    $sum = $sum + $n;
}
print "Sum: $sum\n";

# Sort
@nums = sort { $a <=> $b } @nums;
print "Sorted: " . join(", ", @nums) . "\n";
