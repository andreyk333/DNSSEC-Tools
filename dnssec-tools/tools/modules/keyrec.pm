#
# DNSSEC Tools
#
#	Keyrec file routines.
#
#	The routines in this module manipulate a keyrec file for the DNSSEC
#	tools.  The keyrec file contains information about the values used
#	to generate a key or to sign a zone.
#
#	Entries in the configuration file are of the "key value" format, with
#	the value enclosed in quotes.  Comments may be included by prefacing
#	them with the '#' or ';' comment characters.
#
#	The format and contents of a keyrec file are *very* preliminary.
#	It is assumed that there entries will be grouped into one of two
#	types of records.  A zone record contains data used to sign a zone.
#	A key record contains data used to generate an encryption key.  Each
#	record type has several subfields.
#
#	An example configuration file follows:
#
#		zone "isles.netsec.tislabs.com"
#			zonefile	"db.isles.netsec.tislabs.com"
#			kskpath		"Kisles.netsec.tislabs.com.+005+26000"
#			zskpath		"Kisles.netsec.tislabs.com.+005+52000"
#			endtime		"+2592000"   # Zone expires in 30 days.
#
#		key "Kisles.netsec.tislabs.com.+005+26000"
#			zonename	"isles.netsec.tislabs.com"
#			type		"ksk"
#			algorithm	"rsasha1"
#			length		"1024"
#			random		"-r /dev/urandom"
#
#	The current implementation assumes that only one keyrec file will
#	be open at a time.  If module use proves this to be a naive assumption
#	this module will have to be rewritten to account for it.
#

use strict;

my @keyreclines;			# Keyrec lines.
my $keyreclen;				# Number of keyrec lines.

my %keyrecs = ();			# Keyrec hash table (keywords/values.)

my $modified;				# File-modified flag.


#--------------------------------------------------------------------------
#
# Routine:	keyrec_read()
#
# Purpose:	Read a DNSSEC keyrec file.  The contents are read into the
#		@keyreclines array and the keyrecs are broken out into the
#		%keyrecs hash table.
#
sub keyrec_read
{
	my $krf = shift;		# Key record file.
	my $name;			# Name of the keyrec (zone or key.)
	my @sbuf;			# Buffer for stat().

	#
	# Make sure the keyrec file exists.
	#
	if(! -e $krf)
	{
		print STDERR "$krf does not exist\n";
		return(-1);
	}

	#
	# If a keyrec file is already open, we'll flush our buffers and
	# save the file.
	#
	@sbuf = stat(KEYREC);
	if(@sbuf != 0)
	{
		keyrec_save();
	}

	#
	# Open up the keyrec file.
	#
	if(open(KEYREC,"+< $krf") == 0)
	{
		print STDERR "unable to open $krf\n";
		return(-1);
	}

	#
	# Initialize some data.
	#
	@keyreclines = ();
	$keyreclen   = 0;
	$modified    = 0;

	#
	# Grab the lines and pop 'em into the keyreclines array.  We'll also
	# save each keyrec into a hash table for easy reference.
	#
	while(<KEYREC>)
	{
		my $line;		# Line from the keyrec file.
		my $keyword = "";	# Keyword from the line.
		my $value = "";		# Keyword's value.

		$line = $_;

		#
		# Save the line in our array of keyrec lines.
		#
		$keyreclines[$keyreclen] = $line;
		$keyreclen++;

		#
		# Skip comment lines and empty lines.
		#
		if(($line =~ /^[ \t]*$/) || ($line =~ /^[ \t]*[;#]/))
		{
			next;
		}

		#
		# Grab the keyword and value from the line.  The keyword
		# must be alphabetic.  The value can contain alphanumerics,
		# and a number of punctuation characters.  The value *must*
		# be enclosed in double quotes.
		#
		$line =~ /^[ \t]*([a-zA-Z]+)[ \t]+"([a-zA-Z0-9\/\-+_., ]+)"/;
		$keyword = $1;
		$value = $2;
#		print "keyrec_read:  keyword <$keyword>\t\t<$value>\n";

		#
		# If the keyword is "key" or "zone", then we're starting a
		# new record.  We'll save the name of the keyrec, as well
		# as the record type, and then proceed on to the next line.  
		#
		if(($keyword =~ /^key$/i) || ($keyword =~ /^zone$/i))
		{
			$name = $value;

			#
			# If this name has already been used for a keyrec,
			# we'll whinge, clean up, and return.  No keyrecs
			# will be retained.
			#
			if(exists($keyrecs{$name}))
			{
				print STDERR "keyrec_read:  duplicate record name; aborting...\n";

				@keyreclines = ();
				$keyreclen = 0;
				%keyrecs = ();

				close(KEYREC);
				return(0);
			}
			keyrec_newkeyrec($name,$keyword);
			next;
		}

		#
		# Save this subfield into the keyrec's collection.
		#
		$keyrecs{$name}{$keyword} = $value;
	}

#	print "\n";

	#
	# Return the number of keyrecs we found.
	#
	return($keyreclen);
}

#--------------------------------------------------------------------------
#
# Routine:	keyrec_names()
#
# Purpose:	Smoosh the keyrec names into an array and return the array.
#
sub keyrec_names
{
	my $krn;				# Keyrec name index.
	my @names = ();				# Array for keyrec names.

	foreach $krn (sort(keys(%keyrecs)))
	{
		push @names, $krn;
	}

	return(@names);
}

#--------------------------------------------------------------------------
#
# Routine:	keyrec_fullrec()
#
# Purpose:	Return all entries in a given keyrec.
#
sub keyrec_fullrec
{
	my $name = shift;
	my $krec = $keyrecs{$name};

	return($krec);
}

#--------------------------------------------------------------------------
#
# Routine:	keyrec_recval()
#
# Purpose:	Return the value of a name/subfield pair.
#
sub keyrec_recval
{
	my $name = shift;
	my $field = shift;
	my $val = $keyrecs{$name}{$field};

	return($val);
}

#--------------------------------------------------------------------------
#
# Routine:	keyrec_setval()
#
# Purpose:	Set the value of a name/subfield pair.
#
sub keyrec_setval
{
	my $found = 0;					# Keyrec-found flag.
	my $fldind;				# Loop index.
	my $krind;			# Loop index for finding keyrec.
	my $name  = shift;		# Name of keyrec we're modifying.
	my $field = shift;		# Keyrec's subfield to be changed.
	my $val	  = shift;		# New value for the keyrec's subfield.

	#
	# If a keyrec of the specified name doesn't exist, then we'll whine
	# and return an empty string.
	#
	# Unless...  If the field is "keyrec_type", then we're creating
	# a new keyrec.  We'll add it to the @keyreclines array and the
	# %keyrecs hash.
	#
	if(!exists($keyrecs{$name}))
	{
		#
		# If we aren't creating a new keyrec, whine and return.
		#
		if($field ne "keyrec_type")
		{
			print STDERR "keyrec_setval:  non-existent keyrec \"$name\"\n";
			return("");
		}

		#
		# Add the keyrec to the %keyrecs hash.
		#
		keyrec_newkeyrec($name,$val);

		#
		# Start the new keyrec in @keyreclines.
		#
		$keyreclines[$keyreclen] = "\n";
		$keyreclen++;
		$keyreclines[$keyreclen] = "$val\t\"$name\"\n";
		$keyreclen++;

		#
		# Set the file-modified flag.
		#
		$modified = 1;
		
		return;
	}

	#
	# Set the new value for the name/field in %keyrecs.
	#
	$keyrecs{$name}{$field} = $val;

	#
	# Find the appropriate entry to modify in @keyreclines.  If the
	# given field isn't set in $name's keyrec, we'll insert this as
	# a new field at the end of that keyrec.
	#
	for($krind=0;$krind<$keyreclen;$krind++)
	{
		my $line = $keyreclines[$krind];	# Line in keyrec file.
		my $krtype;				# Keyrec type.
		my $krname;				# Keyrec name.

		#
		# Dig out the line's keyword and value.
		#
		$line =~ /^[ \t]*([a-zA-Z]+)[ \t]+"([a-zA-Z0-9\/\-+_., ]+)"/;
		$krtype = $1;
		$krname = $2;

		#
		# If this line has the keyrec's name and is the start of a
		# new keyrec, then we've found our man.
		#
		# IMPORTANT NOTE:  We will *always* find the keyrec we're
		#		   looking for.  The exists() check above
		#		   ensures that there will be a keyrec with
		#		   the name we want.
		#
		if(($krname eq $name) &&
		   (($krtype eq "zone") || ($krtype eq "key")))
		{
			last;
		}
	}


	my $lastfld = 0;
	for($fldind=$krind+1;$fldind<$keyreclen;$fldind++)
	{
		my $line = $keyreclines[$fldind];	# Line in keyrec file.
		my $lkw;				# Line's keyword.
		my $lval;				# Line's value.

		#
		# Get the line's keyword and value.
		#
		$line =~ /^[ \t]*([a-zA-Z]+)[ \t]+"([a-zA-Z0-9\/\-+_., ]+)"/;
		$lkw = $1;
		$lval = $2;

		#
		# If we hit the beginning of the next keyrec without
		# finding the field, drop out and insert it.
		#
		if($lkw eq "")
		{
			next;
		}

		#
		# If we hit the beginning of the next keyrec without
		# finding the field, drop out and insert it.
		#
		if(($lkw eq "zone") || ($lkw eq "key"))
		{
			last;
		}

		$lastfld = $fldind;

		#
		# If we found the field, set the found flag, drop out and
		# modify it.
		#
		if($lkw eq $field)
		{
			$found = 1;
			last;
		}
	}

	
	#
	# If we found the entry, we'll modify it in place.
	# If we didn't find the entry, we'll insert a new line into the array.
	#
	if($found)
	{
		$keyreclines[$fldind] =~ s/"([a-zA-Z0-9\/\-+_., ]+)"/"$val"/;
	}
	else
	{
		my $newline = "\t$field\t\t\"$val\"\n";
		my @endarr = splice @keyreclines,$fldind-1;
		push @keyreclines,$newline;
		push @keyreclines,@endarr;
	}

	$modified = 1;
}

#--------------------------------------------------------------------------
#
# Routine:	keyrec_newkeyrec()
#
# Purpose:	Display the key record file contents.
sub keyrec_newkeyrec
{
	my $name = shift;		# Name of keyrec we're creating.
	my $type  = shift;		# Type of keyrec we're creating.

	$keyrecs{$name}{"keyrec_name"} = $name;
	$keyrecs{$name}{"keyrec_type"} = $type;
}

#--------------------------------------------------------------------------
#
# Routine:	keyrec_save()
#
# Purpose:	Save the key record file and close the descriptor.
#
sub keyrec_save
{
	keyrec_write();
	close(KEYREC);
}

#--------------------------------------------------------------------------
#
# Routine:	keyrec_write()
#
# Purpose:	Save the key record file and leave the file handle open.
#
sub keyrec_write
{
	my $krc = "";		# Concatenated keyrec file contents.

	#
	# If the file hasn't changed, we'll skip writing.
	#
	if(!$modified)
	{
		return;
	}

	#
	# Loop through the array of keyrec lines and concatenate them all.
	#
	for(my $ind=0;$ind<$keyreclen;$ind++)
	{
		$krc .= $keyreclines[$ind];
	}

	#
	# Zap the keyrec file and write out the new one.
	#
	seek(KEYREC,0,0);
	truncate(KEYREC,0);
	print KEYREC $krc;
}

#--------------------------------------------------------------------------
#
# Routine:	keyrec_dump()
#
# Purpose:	Dump the parsed keyrec entries.
#
sub keyrec_dump
{
	#
	# Loop through the hash of keyrecs and print the keyrec names,
	# subfields, and values.
	#
	foreach my $k (sort(keys(%keyrecs)))
	{
		print "keyrec - $k\n";
		my $subp = $keyrecs{$k};
		my %subrecs = %$subp;
		foreach my $sk (sort(keys(%subrecs)))
		{
			print "\t$sk\t\t$subrecs{$sk}\n";
		}
		print "\n";
	}
}

#--------------------------------------------------------------------------
#
# Routine:	keyrec_list()
#
# Purpose:	Display the key record file contents.
#
sub keyrec_list
{
	#
	# Loop through the array of keyrec lines and print them all.
	#
	for(my $ind=0;$ind<$keyreclen;$ind++)
	{
		print $keyreclines[$ind];
	}
}

1;

#############################################################################

=pod

=head1 NAME

DNSSEC::keyrec - Squoodge around with a DNSSEC tools keyrec file.

=head1 SYNOPSIS

  use DNSSEC::keyrec;

  keyrec_read("localzone.keyrec");

  @krnames = keyrec_names();

  $krec = keyrec_fullrec("portrigh.com");
  %keyhash = %$krec;
  $zname = $keyhash{"algorith"};

  $val = keyrec_recval("portrigh.com","zonefile");

  keyrec_setval("portrigh.com","zonefile","db.portrigh.com");

  keyrec_save();
  keyrec_write();

  keyrec_dump();
  keyrec_list();

=head1 DESCRIPTION

TBD

=head2 Keyrec Format

=head1 KEYREC INTERFACES

=head2 I<keyrec_fullrec>()

=head2 I<keyrec_names>()

=head2 I<keyrec_read>()

=head2 I<keyrec_recval>()

=head2 I<keyrec_save>()

=head2 I<keyrec_setval>()

=head2 I<keyrec_write>()


=head2 I<keyrec_dump>()

=head2 I<keyrec_list>()

=head1 EXAMPLES

TBD

=head1 AUTHOR

Wayne Morrison, tewok@users.sourceforge.net

=head1 SEE ALSO

appropriate other stuff

=cut
