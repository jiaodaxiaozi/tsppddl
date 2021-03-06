#!/bin/bash

# Example usage:
# ./launch_jobs.sh tsppd branch_and_cut
# params_file=../params/go_only.json ./launch_jobs.sh tsppddl tabu_and_branch_and_cut

data_dir="../data/new/"
stdout_dir="../output/stdout/"
stderr_dir="../output/stderr/"
exec_file="../build/tsppddl"

if [[ -z "$params_file" ]]
then
    echo "Using default params file"
    params_file="../params/default.json"
else
    echo "Using params file $params_file"
fi

excluded_instances=(
".*_10_.*_.*\.json"
".*_14_.*_.*\.json"
".*_18_.*_.*\.json"
".*_22_.*_.*\.json"
)

contains_element () {
    for element in "${@:2}"
    do
      	[[ "$1" =~ ${element} ]] && return 0
    done

    return 1
}

check_the_user_didnt_forget_excluded_instances() {
    if [[ ${#excluded_instances[@]} -eq 0 ]]
    then
        echo "No excluded instances, proceeding..."
    else
        read -p "Warning: I have a list of excluded instances: proceed? y/n " -n 1 -r
        echo

        if [[ "$REPLY" =~ ^[Yy]$ ]]
        then
            echo "Ok, proceeding..."
        else
            echo "Ok, aborting..."
            exit
        fi
    fi
}

create_output_dirs() {
    mkdir -p "$stdout_dir"
    mkdir -p "$stderr_dir"
}

schedule_jobs() {
    for file in $(printf "%s%s" "$data_dir" "/*")
    do
        filename=$(basename "$file")
        extension="${filename##*.}"
        instance="${filename%.*}"

        if [[ "$extension" != "json" ]]
        then
            echo "Instance file is not json!"
            exit
        fi

        if contains_element "$filename" "${excluded_instances[@]}"
        then
           	echo "Skipping $filename"
        else
            stdout_file=$(printf "%s%s%s" "$stdout_dir" "$instance" ".stdout")
            stderr_file=$(printf "%s%s%s" "$stderr_dir" "$instance" ".stderr")

            oarsub -n "$instance" -O "$stdout_file" -E "$stderr_file" -p "network_address!='drbl10-201-201-21'" -l /nodes=1/core=1,walltime=96 "$exec_file $file $params_file tabu_and_branch_and_cut"
        fi
    done
}

check_the_user_didnt_forget_excluded_instances
create_output_dirs
schedule_jobs "$*"