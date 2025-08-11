describe 'database' do
  def run_script(commands)
    raw_output = nil
    IO.popen("./my_sql_app", "r+") do |pipe|
      commands.each do |command|
        pipe.puts command
      end

      pipe.close_write

      # Read entire output
      raw_output = pipe.gets(nil)
    end
    output = raw_output.split("
")
    # The first 19 lines are the startup screen.
    # The first prompt is on line 20.
    output.slice(19..-1)
  end

  it 'inserts and retrieves a row' do
    result = run_script([
      "insert 1 user1 person1@example.com",
      "select",
      ".exit",
    ])
    expect(result).to match_array([
      "f_yeah_db 🤞🏾> Executed. ",
      "f_yeah_db 🤞🏾> (1, user1, person1@example.com)",
      "Executed. ",
      "f_yeah_db 🤞🏾> ",
    ])
  end

  it 'prints error message when table is full' do
    script = (1..1401).map do |i|
      "insert #{i} user#{i} person#{i}@example.com"
    end
    script << ".exit"
    result = run_script(script)
    expect(result[-2]).to eq('f_yeah_db 🤞🏾> Error: Table full.')
  end

  it 'allows inserting strings that are the maximum length' do
    long_username = "a"*32
    long_email = "a"*255
    script = [
      "insert 1 #{long_username} #{long_email}",
      "select",
      ".exit",
    ]
    result = run_script(script)
    expect(result).to match_array([
      "f_yeah_db 🤞🏾> Executed. ",
      "f_yeah_db 🤞🏾> (1, #{long_username}, #{long_email})",
      "Executed. ",
      "f_yeah_db 🤞🏾> ",
    ])
  end

  it 'prints error message if strings are too long' do
    long_username = "a"*33
    long_email = "a"*256
    script = [
      "insert 1 #{long_username} #{long_email}",
      "select",
      ".exit",
    ]
    result = run_script(script)
    expect(result).to match_array([
      "f_yeah_db 🤞🏾> String is too long.",
      "f_yeah_db 🤞🏾> Executed. ",
      "f_yeah_db 🤞🏾> ",
    ])
  end

  it 'prints an error message if id is negative' do
    script = [
      "insert -1 cstack foo@bar.com",
      "select",
      ".exit",
    ]
    result = run_script(script)
    expect(result).to match_array([
      "f_yeah_db 🤞🏾> ID must be positive.",
      "f_yeah_db 🤞🏾> Executed. ",
      "f_yeah_db 🤞🏾> ",
    ])
  end

  it 'keeps data after closing connection' do
    result1 = run_script([
      "insert 1 user1 person1@example.com",
      ".exit"match_array
    ])
    expect (result1).to match_array([
      "f_yeah_db 🤞🏾> Executed.",
      "f_yeah_db 🤞🏾> ",
    ])
     result2 = run_script([
    "select",
    ".exit",
  ])
  expect(result2).to match_array([
    "db > (1, user1, person1@example.com)",
    "Executed.",
    "db > ",
  ])
  end
end
